#include <windows.h>
#include "app/App.h"

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    App app;
    return app.Run(inst);
}
