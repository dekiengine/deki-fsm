#pragma once

// DLL export macro, in its own header so the package's headers can use it
// without including FsmPackage.h. That header includes everything, so using
// it here creates include cycles that leave types undefined.
#ifdef DEKI_EDITOR
#ifdef _WIN32
#ifdef DEKI_FSM_EXPORTS
#define DEKI_FSM_API __declspec(dllexport)
#else
#define DEKI_FSM_API __declspec(dllimport)
#endif
#else
#define DEKI_FSM_API
#endif
#else
#define DEKI_FSM_API
#endif
