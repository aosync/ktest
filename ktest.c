#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>

struct k_handler {
	void (*h)(int fd);
};

int running = 1;
struct kevent *klist = NULL;
size_t klistlen = 0;
size_t klistcap = 0;

void k_init(void);
void k_end(void);
void k_add(struct kevent kevent);
void k_once(int fd, int ev, struct k_handler *k_handler);
void k_on(int fd, int ev, struct k_handler *k_handler);
void sres(int fd);
void saccept(int fd);

void k_init(void) {
	klist = malloc(sizeof(struct kevent));
	klistcap = 1;
}
void k_end(void) {
	free(klist);
	klistlen = 0;
	klistcap = 0;
}

void k_add(struct kevent kevent) {
	klist[klistlen++] = kevent;
	if (klistlen >= klistcap) {
		klistcap *= 2;
	}
	klist = realloc(klist, sizeof(struct kevent)*klistcap);
}

void k_once(int fd, int ev, struct k_handler *k_handler) {
	struct kevent kev;
	kev.ident = fd;
	kev.filter = ev;
	kev.flags = EV_ADD | EV_ONESHOT;
	kev.udata = k_handler;
	k_add(kev);
}

void k_on(int fd, int ev, struct k_handler *k_handler) {
	struct kevent kev;
	kev.ident = fd;
	kev.filter = ev;
	kev.flags = EV_ADD;
	kev.udata = k_handler;
	k_add(kev);
}

void k_flush(void) {
	klist = realloc(klist, sizeof(struct kevent));
	klistlen = 0;
	klistcap = 1;
}

int panic(char *msg) {
	fprintf(stderr, "ktest: %s\n", msg);
	exit(1);
}

struct k_handler handlers[2] = {
	{.h = sres},
	{.h = saccept},
};

void sres(int fd) {
	char buf[16];
	int r = read(fd, buf, 16);
	if (r == 0) {
		close(fd);
		return;
	}
	if (r == 2 && buf[0] == 'q') {
		running = 0;
		close(fd);
		return;
	}
	write(fd, buf, r);
	k_once(fd, EVFILT_READ, &handlers[0]);
}



void saccept(int fd) {
	printf("lmfao wat\n");
	struct sockaddr caddr;
	socklen_t caddrlen = sizeof(caddr);
	int cfd = accept(fd, &caddr, &caddrlen);
	printf("accepted\n");
	fcntl(cfd, F_SETFL, O_NONBLOCK);

	k_once(cfd, EVFILT_READ, &handlers[0]);
}



int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		panic("could not open server socket");

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8000);
	inet_aton("127.0.0.1", (struct in_addr*)&addr.sin_addr.s_addr);
	int ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));

	ret = listen(fd, 128);
	if (ret == -1)
		panic("could not initiate listening on server socket");

	int kq = kqueue();
	k_init();
	k_on(fd, EVFILT_READ, &handlers[1]);

	while (running) {
		struct kevent res[16];
		size_t n = kevent(kq, klist, klistlen, res, 16, NULL);
		printf("kevented\n");
		k_flush();
		for (size_t i = 0; i < n; i++)
			((struct k_handler*)res[i].udata)->h(res[i].ident);
	}


	k_end();
	close(kq);
	close(fd);
}
