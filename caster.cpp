#include <unistd.h> 
#include <fcntl.h> 
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/epoll.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 

#include "caster.h"
#include "util.h"

ntrip_caster::ntrip_caster()
{
	m_listen_sock = 0;
	m_epoll_fd = 0;
	m_max_count = 0;
	m_epoll_events = NULL;
}
 
ntrip_caster::~ntrip_caster()
{
	if(m_listen_sock > 0){
		close(m_listen_sock);
	}

	if(m_epoll_fd > 0){
		close(m_epoll_fd);
	}
}
 
bool ntrip_caster::init(int port , int sock_count)
{
	m_max_count = sock_count;
	struct sockaddr_in caster_addr;
	memset(&caster_addr, 0, sizeof(&caster_addr));
	caster_addr.sin_family = AF_INET;
	caster_addr.sin_port = htons(port);
	caster_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	m_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(m_listen_sock == -1) {
		exit(1);
	}

	if(bind(m_listen_sock, (struct sockaddr*)&caster_addr, sizeof(caster_addr)) == -1){
		exit(1);
	}

	if(listen(m_listen_sock, 5) == -1){
		exit(1);
	}
											 
	m_epoll_events = new struct epoll_event[sock_count];
	if (m_epoll_events == NULL){
		exit(1);
	}

	m_epoll_fd = epoll_create(sock_count);
	epoll_ops(m_listen_sock, EPOLL_CTL_ADD, EPOLLIN);

	mntlist_head = NULL;

	return true;
}
 
bool ntrip_caster::init(const char *ip, int port , int sock_count)
{	
	m_max_count = sock_count;
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(&server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(ip);		

	m_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(m_listen_sock == -1){
		exit(1);
	}

	if(bind(m_listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
		exit(1);
	}
											
	if(listen(m_listen_sock, 5) == -1){
		exit(1);
	}

	m_epoll_events = new struct epoll_event[sock_count];
	if (m_epoll_events == NULL){
		exit(1);
	}

	m_epoll_fd = epoll_create(sock_count);
	epoll_ops(m_listen_sock, EPOLL_CTL_ADD, EPOLLIN);

	mntlist_head = NULL;

	return true;
}
 
int ntrip_caster::accept_new_client()
{
	sockaddr_in client_addr;
	memset(&client_addr, 0, sizeof(client_addr));
	socklen_t clilen = sizeof(struct sockaddr); 

	int new_sock = accept(m_listen_sock, (struct sockaddr*)&client_addr, &clilen);	
	epoll_ops(new_sock, EPOLL_CTL_ADD, EPOLLIN);
	
	return new_sock;
}
 
int ntrip_caster::recv_data(int sock, char *recv_buf)
{
	char buf[1024] = {0};
	int len = 0;
	int ret = 0;

	while(ret >= 0)
	{
		ret = recv(sock, buf, sizeof(buf), 0);
		if(ret <= 0)
		{
			epoll_ops(sock, EPOLL_CTL_DEL, EPOLLERR);
			close(sock);
			break;
		}else if(ret < 1024){
			memcpy(recv_buf, buf, ret);
			len += ret;	
			break;
		}else{
			memcpy(recv_buf, buf, sizeof(buf));
			len += ret;
		}
	}

	return ret <= 0 ? ret : len;
}
 
int ntrip_caster::send_data(int sock, const char *send_buf, int buf_len)
{
	int len = 0;
	int ret = 0;

	while(len < buf_len){
		if(buf_len < 1024)
			ret = send(sock, send_buf + len, buf_len, 0);
		else
			ret = send(sock, send_buf + len, 1024, 0);
		if(ret <= 0) {
			epoll_ops(sock, EPOLL_CTL_DEL, EPOLLERR);
			close(sock);
			break;
		}else{
			len += ret;
		}

		if(ret < 1024){
			break;
		}
	}

	if(ret > 0){
		epoll_ops(sock, EPOLL_CTL_MOD, EPOLLIN);
	}
		
	return ret <= 0 ? ret : len;
}

int ntrip_caster::ntrip_caster_wait(int time_out)
{
	int nfds = epoll_wait(m_epoll_fd, m_epoll_events, m_max_count, time_out);
}
 
void ntrip_caster::run(int time_out)
{
	char *recv_buf = new char[MAX_LEN];
	char *send_buf = new char[MAX_LEN];

	while(1){
		int ret = ntrip_caster_wait(time_out);
		if(ret == 0){
			cout << "time out" << endl;
			continue;
		}else if(ret == -1){
			cout << "error" << endl;
		} else{
			for(int i = 0; i < ret; i++){
				if(m_epoll_events[i].data.fd == m_listen_sock){
					if(m_epoll_events[i].events & EPOLLIN){
						int new_sock = accept_new_client();
					}
				}else{
					if(m_epoll_events[i].events & EPOLLIN){
						int recv_count = recv_data(m_epoll_events[i].data.fd, recv_buf);
						if(recv_count == 0){
							int sock = m_epoll_events[i].data.fd;
							struct mnt_info *mi_ptr = check_conn(sock);
							if(mi_ptr && mi_ptr->current_conn_cursor != -1){
								del_conn(mi_ptr);
							}

							if(check_mntpoint(sock, NULL) != NULL){
								del_mntpoint(sock);
							}

							if(!epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, sock, &m_epoll_events[i])){
								close(sock);
							}
							continue;
						}

						if(recv_count < 0)
							continue;

						memcpy(send_buf, recv_buf, recv_count);
						parse_data(m_epoll_events[i].data.fd, recv_buf, recv_count);
						memset(recv_buf, 0, strlen(recv_buf));
					}else if(m_epoll_events[i].events & EPOLLOUT){
						int send_count = send_data(m_epoll_events[i].data.fd, send_buf, strlen(send_buf));
						memset(send_buf, 0, send_count);
					}
				}
			}
		}
	}
}

void ntrip_caster::epoll_ops(int sock, int op, uint32_t events)
{
	struct epoll_event ev;

	ev.data.fd = sock;
	ev.events  = events;
	epoll_ctl(m_epoll_fd, op, sock, &ev);
}

int ntrip_caster::add_conn(struct mnt_info *mnt_node, int client_sock)
{
	int ret = 0;
	if(mnt_node->current_conn_count >= MAX_CONN){
		ret = -1;
	}else{
		mnt_node->conn_sock[mnt_node->current_conn_count] = client_sock;
		mnt_node->current_conn_count++;
		cout << "add conn ok" << endl;
	}

	return ret;
}

int ntrip_caster::del_conn(struct mnt_info *mnt_node)
{
	int ret = -1;
	if(mnt_node == NULL)
		return ret;

	if(mnt_node->current_conn_cursor > -1){
		for(int i=mnt_node->current_conn_cursor; i<MAX_CONN-1; ++i){
			mnt_node->conn_sock[i] = mnt_node->conn_sock[i+1];
			if(mnt_node->conn_sock[i] == 0)
					break;
		}
		mnt_node->current_conn_count--;
		mnt_node->current_conn_cursor = -1;
		ret = 0;
	}

	return ret;
}

struct mnt_info* ntrip_caster::check_conn(int client_sock)
{

	struct mnt_list *ml_ptr = mntlist_head;
	if(ml_ptr == NULL)
		return NULL;

	while(ml_ptr != NULL){
		for(int i = 0; i < ml_ptr->mi->current_conn_count; ++i){
			if(client_sock == ml_ptr->mi->conn_sock[i])
				ml_ptr->mi->current_conn_cursor = i;
				return ml_ptr->mi;
		}

		if(ml_ptr->next == NULL)
				break;
		ml_ptr = ml_ptr->next;
	}

	return NULL;
}

int ntrip_caster::add_mntpoint(int server_sock, const char *mnt_name , const char *usr, const char *pwd)
{
	struct mnt_list *ml_node = new mnt_list();
	struct mnt_info *mnt = new mnt_info();
	mnt->server_fd = server_sock;
	strncpy(mnt->mntname, mnt_name, strlen(mnt_name));
	strncpy(mnt->username, usr, strlen(usr));
	strncpy(mnt->password, pwd, strlen(pwd));
	mnt->current_conn_count = 0;
	mnt->current_conn_cursor = -1;

	ml_node->mi = mnt;
	ml_node->next = NULL;
	
	if(mntlist_head == NULL){
		mntlist_head = ml_node;
	}else{
		struct mnt_list *ml_ptr = mntlist_head;
		while(ml_ptr->next != NULL)
			ml_ptr = ml_ptr->next;
		ml_ptr->next = ml_node;
	}

	return 0;
}

int ntrip_caster::del_mntpoint(int server_sock)
{
	int ret = -1;
	struct mnt_list *ml_ptr= mntlist_head;
	struct mnt_list *ml_prev= ml_ptr;

	if(ml_ptr == NULL){
		ret = 0;
	}else if(ml_ptr->next == NULL){
		if(ml_ptr->mi->server_fd == server_sock){
			mntlist_head = NULL;
			delete(ml_ptr->mi);
			delete(ml_ptr);
			ret = 0;
		}
	}else if(ml_ptr->next->next == NULL){
		ml_ptr = ml_ptr->next;
		if(ml_prev->mi->server_fd == server_sock){
			mntlist_head = ml_ptr;
			delete(ml_prev->mi);
			delete(ml_prev);
			ret = 0;
		}else if(ml_ptr->mi->server_fd == server_sock){
			ml_prev->next = NULL;
			delete(ml_ptr->mi);
			delete(ml_ptr);
			ret = 0;
		}
	}else{
		ml_ptr = ml_ptr->next;
		if(ml_prev->mi->server_fd == server_sock){
			mntlist_head = ml_ptr;
			delete(ml_prev->mi);
			delete(ml_prev);
			ret = 0;
		}else{
			while(ml_ptr != NULL){
				if(ml_ptr->mi->server_fd == server_sock){
					ml_prev->next = ml_ptr->next;
					delete(ml_ptr->mi);
					delete(ml_ptr);
					ret = 0;
					break;
				}
				ml_prev = ml_ptr;
				ml_ptr = ml_ptr->next;
			}
		}
	}

	return ret;
}

struct mnt_info *ntrip_caster::check_mntpoint(int server_sock, const char *mnt_name)
{
	struct mnt_list *ml_ptr = mntlist_head;
	if(ml_ptr == NULL)
		return NULL;

	while(ml_ptr != NULL){
		if((ml_ptr->mi->server_fd == server_sock) || 
				((mnt_name != NULL) && !strncmp(ml_ptr->mi->mntname, mnt_name, strlen(mnt_name)))){
			return ml_ptr->mi;
		}

		if(ml_ptr->next == NULL)
			break;

		ml_ptr = ml_ptr->next;
	}

	return NULL;
}

int ntrip_caster::parse_data(int sock, char* recv_data, int data_len)
{
	char *temp = recv_data;
	char *result = NULL;
	char m_mnt[16] = {0};
	char m_mntusr[16] = {0};
	char m_mntpwd[16] = {0};
	char m_userpwd[48] = {0};
	char m_username[16] = {0};
	char m_password[16] = {0};

	cout << "recv data:" << endl;
	print_char(recv_data, data_len);

	result = strtok(temp, "\n");
	if(result != NULL) {
		/* Server request to connect to Caster. */
		if(!strncasecmp(result, "POST /", 6)){
			sscanf(result+6, "%[^ ]", m_mnt);
			struct mnt_info *mi_ptr = check_mntpoint(-1, m_mnt);
			if(mi_ptr != NULL){
				cout << "MountPoint already used!!!" << endl;
				send_data(sock, "ERROR - Bad Password\r\n", 22);
				return -1;
			}

			result = strtok(NULL, "\n");
			while(result != NULL) {
				if(!strncasecmp(result, "Authorization: Basic", 20)){
					sscanf(result+21, "%[^\r]", m_userpwd);
					if(strlen(m_userpwd) > 0 ){
						base64_decode(m_userpwd, m_mntusr, m_password);
						add_mntpoint(sock, m_mnt, m_mntusr, m_mntpwd);
						send_data(sock, "ICY 200 OK\r\n", 12);
						return 0;
					}
				}
				result = strtok(NULL, "\n");
			}
		}

		/* Data sent by Server, it needs to be forwarded to connected Client. */
		if(struct mnt_info *mi_ptr = check_mntpoint(sock, NULL)){
			if(mi_ptr != NULL){
				for(int i = 0; i < mi_ptr->current_conn_count; ++i){
					if(mi_ptr->conn_sock[i] > 0)
						send_data(mi_ptr->conn_sock[i], recv_data, data_len);
				}
			}
			return 0;
		}

		/* Client request to get the data of source table. */
		if(!strncasecmp(result, "GET / HTTP/1.1", 14)){
			char *st_data = new char[MAX_LEN];
			get_sourcetable(st_data, MAX_LEN);
			send_data(sock, st_data, strlen(st_data));
			return  0;
		}

		/* Client request to get the Server data. */
		if(!strncasecmp(result, "GET /", 5) && NULL != strstr(result, "HTTP/1.1")){
			sscanf(result, "GET /%s HTTP/1.1", m_mnt);
			struct mnt_info *mi_ptr = check_mntpoint(-1, m_mnt);
			if(mi_ptr == NULL){
				cout << "MountPoint not find!!!" << endl;
				send_data(sock, "HTTP/1.1 401 Unauthorized\r\n", 27);
				return -1;
			}

			result = strtok(NULL, "\n");

			while(result != NULL) {
				if(!strncasecmp(result, "Authorization: Basic", 20)){
					sscanf(result+21, "%[^\r]", m_userpwd);
					if(strlen(m_userpwd) > 0 ){
						base64_decode(m_userpwd, m_username, m_password);
						if(!strncmp(mi_ptr->username, m_username, strlen(m_username)) &&
								!strncmp(mi_ptr->password, m_password, strlen(m_password))){
							cout << "Password error!!!" << endl;
							send_data(sock, "HTTP/1.1 401 Unauthorized\r\n", 27);
							return  -1;
						}

						add_conn(mi_ptr, sock);
						send_data(sock, "ICY 200 OK\r\n", 12);
						return 0;
					}
				}
				result = strtok(NULL, "\n");
			}
		}

		/* If Caster as a VPS, it needs to get the GGA data sent by the Client*/
		if(!strncasecmp(result, "$GPGGA,", 7)){
			if(!check_sum(result)){
				cout << "check pass" << endl;
				/* do something. */
				return 0;
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	ntrip_caster my_caster;
	my_caster.init(12345, 30);
	//my_epoll.init("127.0.0.1", 12345, 10);
	my_caster.run(2000);

	return  0;
}

