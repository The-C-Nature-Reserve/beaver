all:
	gcc beaver.c -o beaver -g -Wall -pedantic

pp:
	gcc beaver.c -DBV_ASYNC_ -g -Wall -pedantic -o beaver
