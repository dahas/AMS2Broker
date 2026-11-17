// Der folgende ifdef-Block ist die Standardmethode zum Erstellen von Makros, die das Exportieren
// aus einer DLL vereinfachen. Alle Dateien in dieser DLL werden mit dem PROXYDLL_EXPORTS-Symbol
// (in der Befehlszeile definiert) kompiliert. Dieses Symbol darf für kein Projekt definiert werden,
// das diese DLL verwendet. Alle anderen Projekte, deren Quelldateien diese Datei beinhalten, sehen
// PROXYDLL_API-Funktionen als aus einer DLL importiert an, während diese DLL
// mit diesem Makro definierte Symbole als exportiert ansieht.
#ifdef PROXYDLL_EXPORTS
#define PROXYDLL_API __declspec(dllexport)
#else
#define PROXYDLL_API __declspec(dllimport)
#endif

// Diese Klasse wird aus der DLL exportiert.
class PROXYDLL_API CProxyDLL {
public:
	CProxyDLL(void);
	// TODO: Methoden hier hinzufügen.
};

extern PROXYDLL_API int nProxyDLL;

PROXYDLL_API int fnProxyDLL(void);
