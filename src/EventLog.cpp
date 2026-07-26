#include "EventLog.h"


#include <Preferences.h>



Preferences logPrefs;



String logs[20];

uint8_t indexLog=0;



void EventLog::add(
String message
)
{

logs[indexLog] =
message;


indexLog++;

if(indexLog>=20)
indexLog=0;

}



String EventLog::get()
{

String result;


for(
int i=0;i<20;i++
)
{

if(
logs[i].length()
)
{

result += logs[i];

result += "\n";

}

}


return result;

}