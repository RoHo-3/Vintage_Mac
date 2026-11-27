#include "Processes.r"

/* MultiFinder / System 7 partition: 1 MB preferred, 768 KB minimum.
   Suspend/resume events let us autosave when the user switches apps. */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    1024 * 1024,
    768 * 1024
};

#include "Dialogs.r"

/* "Name the new project:" / "Name the new folder:" (the prompt is set at
   run time through ^0). Centered on a 512 x 342 screen. */
resource 'DLOG' (200) {
    {110, 106, 210, 406},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    200,
    "",
    centerMainScreen
};

resource 'DITL' (200) {
    {
        {68, 214, 88, 284}, Button { enabled, "Create" },
        {68, 130, 88, 200}, Button { enabled, "Cancel" },
        {10, 16, 26, 284},  StaticText { disabled, "^0" },
        {36, 19, 52, 281},  EditText { enabled, "" }
    }
};

/* Focus goal: "Focus for how many minutes?" / "...new words?" (^0). */
resource 'DLOG' (201) {
    {110, 156, 210, 356},
    dBoxProc,
    invisible,
    noGoAway,
    0,
    201,
    "",
    centerMainScreen
};

resource 'DITL' (201) {
    {
        {68, 114, 88, 184}, Button { enabled, "Start" },
        {68, 30, 88, 100},  Button { enabled, "Cancel" },
        {10, 16, 26, 184},  StaticText { disabled, "^0" },
        {36, 19, 52, 81},   EditText { enabled, "" }
    }
};
