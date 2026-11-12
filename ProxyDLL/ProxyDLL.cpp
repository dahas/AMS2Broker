// ProxyDLL.cpp : Hiermit werden die exportierten Funktionen für die DLL definiert.
//

#include "pch.h"
#include "framework.h"
#include "ProxyDLL.h"


// Dies ist ein Beispiel für eine exportierte Variable.
PROXYDLL_API int nProxyDLL=0;

// Dies ist ein Beispiel für eine exportierte Funktion.
PROXYDLL_API int fnProxyDLL(void)
{
    return 0;
}

// Dies ist der Konstruktor einer Klasse, die exportiert wurde.
CProxyDLL::CProxyDLL()
{
    return;
}
