
all : dfs dfc
	echo done

dfs : dfs.o
	gcc -pthread -o dfs dfs.o

dfc : dfc.o md5.o
	gcc -o dfc dfc.o md5.o

dfs.o : dfs.c dfs.h
	gcc -c -Wall -Werror -o dfs.o dfs.c

dfc.o : dfc.c dfs.h md5.h
	gcc -c -Wall -Werror -o dfc.o dfc.c

md5test : md5test.o md5.o
	gcc -o md5test md5test.o md5.o

md5.o : md5.c
	gcc -c -Wall -Werror -o md5.o md5.c


clean :
	rm -f *.o dfs dfc
	rm -rf DFS1 DFS2 DFS3 DFS4
	rm -rf tmp

clear:
	-killall dfs
	rm -rf DFS1 DFS2 DFS3 DFS4

start :
	-killall dfs
	./dfs DFS1 10001 &
	./dfs DFS2 10002 &
	./dfs DFS3 10003 &
	./dfs DFS4 10004 &
	ps u
	echo done

start123 :
	-killall dfs
	./dfs DFS1 10001 &
	./dfs DFS2 10002 &
	./dfs DFS3 10003 &
	ps u
	echo done

start234 :
	-killall dfs
	./dfs DFS2 10002 &
	./dfs DFS3 10003 &
	./dfs DFS4 10004 &
	ps u
	echo done

stop :
	-killall dfs
