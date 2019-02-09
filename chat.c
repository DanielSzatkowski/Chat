#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>

#define MSG_SIZE 	127

int sfd;					/* deskryptor gniazda */

/* jesli signo = 2, funkcja zostala wywolana przez SIGINT, jesli signo = 255, funkcja zostala wywolana by zamknac program normalnie */
void closeProgram(int signo)
{
	close(sfd);

	if(signo == 2)
		printf("\n");

	exit(0);
}

void error(char *txt)
{
	close(sfd);
	fprintf(stderr, "%s\n", txt);
	
	exit(1);
}

struct msg{
	short type;				/* 0 - normalna wiadomosc, 1 - wiadomosc o dolaczeniu do czatu */
	char txt[MSG_SIZE];
	char nick[255];
};

/*////////////////////////////////////////////////*/
/*                     MAIN                       */
/*////////////////////////////////////////////////*/
int main(int argc, char *argv[])
{
	/* dane "podstawowe" */
	char *N;
	N = "7171";

	char * address;		/* host podany jako argument wywolania programu */
	char nick[255];		/* nick uzytkownika */
	struct msg msg_r;	/* struktura wiadomosci  - odbieranie */
	struct msg msg_s;	/* struktura wiadomosci  - wysylanie */
	
	/* dane potrzebne do wykonania funkcji socket i bind */
	struct sockaddr_in loc_addr, foreign_addr;	/* dane o localhoscie potrzebne funkcji bind */
	
	/* informacje o maszynie z ktora mamy sie polaczyc */
	struct addrinfo hints;				/* uzywane do funkcji getaddrinfo() */
	struct addrinfo *dest;				/* adres maszyny z ktora sie komunikujemy (uzywane do funkcji getaddrinfo()) */
	struct addrinfo *rp;				/* uzywany jako iterator */

	struct in_addr addr_for_print;			/* struktura uzywana do wyswietlenia adresu ip rozmowcy na poczatku */	

	/* OBSLUGA SYGNALU */
	signal(SIGINT, closeProgram);

	/* SPRAWDZENIE ARGUMENTOW Z KTORYMI URUCHOMIONY ZOSTAL PROGRAM*/
	if(argc < 2 || argc > 3){
		error("Nieodpowiednia liczba argumentow\n");
	} else if(argc == 2){
		strcpy(nick, "NN");
		nick[2] = '\0';
	} else {
		strcpy(nick, argv[2]);
		nick[strlen(nick)] = '\0';
	}
	address = argv[1];
	
	/* UZYSKIWANIE INFORMACJI O ADRESIE - BEDZIE ON POTRZEBNY POZNIEJ */
	/* uzupelnianie struktury hints */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;	    	/* IPv4 */
	hints.ai_socktype = SOCK_DGRAM; 	/* socket UDP */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          	/* jakikolwiek protokol */
	
	/* szukanie informacji o adresie maszyny z ktora mamy sie polaczyc */
	if(getaddrinfo(address, N, &hints, &dest) != 0){		
		error("Blad funkcji getaddrinfo()");
	}
	
	/* SOCKET */
	if((sfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1){
		error("Blad funkcji socket()");
	}
	
	int option = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1){
		error("Blad funkcji setdockopt()");
	}

	/* BIND */
	loc_addr.sin_family	    = AF_INET;           /* IPv4 */
	loc_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	loc_addr.sin_port        = htons(7171);    /* moj port */
	
	if(bind(sfd, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) == -1){
		error("Blad funkcji bind()");
	}
	
	/* ROZPOCZECIE DZIALANIA CZATU - UZYTY ZOSTAL FORK TAK ABY UZYTKOWNIK MOGL PISAC I ODBIERAC WIADOMOSCI JEDNOCZESNIE */	
	/* wyswietlenie wiadomosci o rozpoczeciu czatu */
	addr_for_print = ((struct sockaddr_in*)dest -> ai_addr) -> sin_addr;
	printf("Rozpoczynam czat z %s. Napisz <koniec> by zakonczyc czat.\n", inet_ntoa(addr_for_print));	

	/* wyswietlanie znaku zachety dla uzytkownika */		
	printf("[%s]> ", nick);
	if(fflush(stdout) == EOF){
		error("Blad funkcji fflush()");
	}

	pid_t child_pid;		/* PID procesu potomnego - potrzebny do sprzaatnia */
	socklen_t foreign_size = sizeof(foreign_addr);	
	if((child_pid = fork()) == 0){
		/* ODBIERANIE WIADOMOSCI */
		while(1){
			if(recvfrom(sfd, &msg_r, sizeof(msg_r), 0, (struct sockaddr*)&foreign_addr, (socklen_t *)&foreign_size) == -1){		
				kill(getppid(), SIGINT);
				error("Blad funkcji recvfrom()");
			}

			/* czy przerwac dzialanie czatu? */
			if(strcmp(msg_r.txt, "<koniec>\n") == 0){
				printf("\n[%s (%s) zakonczyl rozmowe]\n", msg_r.nick, inet_ntoa(foreign_addr.sin_addr));
				printf("[%s]>", nick);
			} else {
				/* dolaczenie do rozmowy czy zwykla wiadomosc? */
				if(msg_r.type == 1){
					printf("\n[%s (%s) dolaczyl do rozmowy]", msg_r.nick, inet_ntoa(foreign_addr.sin_addr));
					printf("\n[%s]>", nick);
				} else {
					printf("\n[%s (%s)]> %s[%s]> ", msg_r.nick, inet_ntoa(foreign_addr.sin_addr) ,msg_r.txt, nick);
				}
			}
			
			if(fflush(stdout) == EOF){
				kill(getppid(), SIGINT);
				error("Blad funkcji fflush()");
			}

			memset((struct msg*)&msg_r, 0, sizeof(msg_r));
		}
	} else {
		/* WYSLANIE WIADOMOSCI O DOLACZENIU */
		char text[MSG_SIZE];
			
		strcpy(text, " ");
		text[1] = '\0';
			
		strcpy(msg_s.txt, text);
		strcpy(msg_s.nick, nick);
		msg_s.type = 1;

		/* tworzenie wiadomosci do wyslania */
		for(rp = dest; rp != NULL; rp = rp -> ai_next){
			if(sendto(sfd, &msg_s, sizeof(msg_s), 0, rp -> ai_addr, rp -> ai_addrlen) != -1){
				break;
			}
		}
			
		if(rp == NULL){
			printf("%s", "Nie udalo sie wyslac wiadomosci\n");
		}

		/* WYSYLANIE WIADOMOSCI */
		while(1){
			char text[MSG_SIZE];
			int dl;
			if((dl = read(0, &text, MSG_SIZE)) < 0){
				kill(child_pid, SIGINT);
				close(sfd);
				error("Blad funkcji read()");
			}
			
			text[dl] = '\0';
			
			strcpy(msg_s.txt, text);
			strcpy(msg_s.nick, nick);
			msg_s.type = 0;

			/* tworzenie wiadomosci do wyslania */
			for(rp = dest; rp != NULL; rp = rp -> ai_next){
				if(sendto(sfd, &msg_s, sizeof(msg_s), 0, rp -> ai_addr, rp -> ai_addrlen) != -1){
					break;
				}
			}
			
			if(rp == NULL){
				printf("%s", "Nie udalo sie wyslac wiadomosci\n");
			}

			/* czy przerwac dzialanie czatu? */
			if(strcmp(text, "<koniec>\n") == 0){
				kill(child_pid, SIGINT);
				closeProgram(255);
			}

			/* wyswietlanie znaku zachety dla uzytkownika */		
			printf("[%s]> ", nick);
			if(fflush(stdout) == EOF){
				kill(child_pid, SIGINT);
				error("Blad funkcji fflush()");
			}
		}
	}
	
	close(sfd);
	
	return 0;
}
