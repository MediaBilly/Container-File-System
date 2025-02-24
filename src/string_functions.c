#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../headers/string_functions.h"

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

string copyString(string str) {
    string ret;
    if ((ret = (string)malloc(strlen(str) + 1)) == NULL) {
        printf("Not enough memory.\n");
        return NULL;
    }
    strcpy(ret,str);
    return ret;
}

int stringAppend(string *str,string substr) {
    if ((*str = realloc(*str,strlen(*str) + strlen(substr) + 1)) == NULL) {
        printf("Not enough memory.\n");
        return 0;
    }
    strcpy(*str + strlen(*str),substr);
    return 1;
}

void DestroyString(string *str) {
    if (*str != NULL) {
        free(*str);
        *str = NULL;
    }
}

char getPromptAnswer() {
    char ans = getchar();
    if (ans != '\n')
        IgnoreRemainingInput();
    return ans;
}

void IgnoreRemainingInput() {
    while (getchar() != '\n');
}
