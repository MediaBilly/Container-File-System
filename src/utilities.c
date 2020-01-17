#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../headers/utilities.h"

#define BUFFER_SIZE 30

string readNextWord(int *lastword) {
    // Ignore whitespace
    char ch = 0;
    while (((ch = fgetc(stdin)) < 33 && ch != '\n'));
    ungetc(ch,stdin);
    char *buf;
    int curBufSize = sizeof(char) * BUFFER_SIZE;
    if ((buf = (string)malloc(curBufSize)) == NULL) {
        return NULL;
    }
    int i = 0;
    // Read word to dynamically allocated buffer letter by letter in order to avoid static array
    while ((ch = fgetc(stdin)) != '\n' && ch != ' ')
    {
        // Buffer full so allocate more space
        if (i == curBufSize) {
            if ((buf = (string)realloc(buf,(BUFFER_SIZE + curBufSize) * sizeof(char))) == NULL) {
                return NULL;
            }
            curBufSize += BUFFER_SIZE * sizeof(char);
        }
        buf[i++] = ch;
    }
    *lastword = ch == '\n';
    // Buffer full so allocate 1 more byte for end of string character to fit
    if (i == curBufSize) {
        if ((buf = (string)realloc(buf,(1 + curBufSize) * sizeof(char))) == NULL) {
            return NULL;
        }
        curBufSize += sizeof(char);
    }
    buf[i] = '\0';
    string word;
    if((word = (string)malloc((strlen(buf) + 1) * sizeof(char))) == NULL) {
        return NULL;
    }
    strcpy(word,buf);
    free(buf);
    return word;
}

void DestroyString(string *str) {
    if (*str != NULL) {
        free(*str);
        *str = NULL;
    }
}
