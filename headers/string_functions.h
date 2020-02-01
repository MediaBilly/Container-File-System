#ifndef UTILITIES_H
#define UTILITIES_H

typedef char* string;

string readNextWord(int*);
string copyString(string);
int stringAppend(string*,string);
void IgnoreRemainingInput();
char getPromptAnswer();
// Removes possible ./ or /. from path start
void DestroyString(string*);

#endif
