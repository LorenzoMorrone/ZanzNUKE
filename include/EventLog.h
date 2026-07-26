#pragma once

#include <Arduino.h>


class EventLog
{

public:

static void add(
String message
);


static String get();



};