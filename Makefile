# src = $(wildcard *.cpp)
# obj = $(patsubst %.cpp, %.o, $(src))
ALL: serverV1.cpp clientV1.cpp myDB.cpp global.cpp HandleClient.cpp
	g++ -o serverV1 serverV1.cpp myDB.cpp global.cpp -lpthread -lmysqlclient -lhiredis -g
	g++ -o clientV1 clientV1.cpp HandleClient.cpp global.cpp -lpthread -lhiredis -g
# a.out: $(obj)
# 	g++ $^ -o $@

# %.o:%.c
# 	g++ -c $< -o $@

# clean:
# 	-rm -rf $(obj) a.out   

serverV1:serverV1.cpp myDB.cpp global.cpp
	g++ -o serverV1 serverV1.cpp myDB.cpp global.cpp -lpthread -lmysqlclient -lhiredis -g 
clientV1:clientV1.cpp HandleClient.cpp global.cpp
	g++ -o clientV1 clientV1.cpp HandleClient.cpp global.cpp -lpthread -lhiredis -g   

## server_new.o:server_new.cpp
##	g++ -c server_new.cpp -o server_new.o
## cilent_new2.o:client_new2.cpp
##	g++ -c client_new.cpp -o client_new2.o 
clean:
	rm serverV1
	rm clientV1
	rm *.o
