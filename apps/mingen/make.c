/*++

Copyright (c) 2016 Minoca Corp.

    This file is licensed under the terms of the GNU General Public License
    version 3. Alternative licensing terms are available. Contact
    info@minocacorp.com for details. See the LICENSE file at the root of this
    project for complete licensing information.

Module Name:

    make.c

Abstract:

    This module implements output support for Make in the Minoca Build
    Generator.

Author:

    Evan Green 8-Feb-2015

Environment:

    Build

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mingen.h"

//
// ---------------------------------------------------------------- Definitions
//

#define MINGEN_MAKE_VARIABLE "$(%s)"
#define MINGEN_MAKE_LINE_CONTINUATION " \\\n        "
#define MINGEN_MAKE_INPUTS "$+"
#define MINGEN_MAKE_OUTPUT "$@"

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

VOID
MingenMakePrintDefaultTargets (
    PMINGEN_CONTEXT Context,
    FILE *File
    );

VOID
MingenMakePrintBuildDirectoriesTarget (
    PMINGEN_CONTEXT Context,
    FILE *File
    );

VOID
MingenMakePrintMakefileTarget (
    PMINGEN_CONTEXT Context,
    FILE *File
    );

VOID
MingenMakePrintWithVariableConversion (
    FILE *File,
    PSTR Command
    );

VOID
MingenMakePrintTargetFile (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_TARGET Target
    );

VOID
MingenMakePrintSource (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_SOURCE Source
    );

VOID
MingenMakePrintPath (
    FILE *File,
    PMINGEN_PATH Path
    );

VOID
MingenMakePrintTreeRoot (
    FILE *File,
    MINGEN_DIRECTORY_TREE Tree
    );

VOID
MingenMakePrintConfig (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_TARGET Target
    );

INT
MingenMakePrintConfigValue (
    FILE *File,
    PCHALK_OBJECT Value
    );

//
// -------------------------------------------------------------------- Globals
//

//
// ------------------------------------------------------------------ Functions
//

INT
MingenCreateMakefile (
    PMINGEN_CONTEXT Context
    )

/*++

Routine Description:

    This routine creates a Makefile out of the build graph.

Arguments:

    Context - Supplies a pointer to the application context.

Return Value:

    0 on success.

    Returns an error number on failure.

--*/

{

    PLIST_ENTRY CurrentEntry;
    FILE *File;
    UINTN Index;
    PMINGEN_TARGET Input;
    PSTR MakefilePath;
    PMINGEN_SCRIPT Script;
    PLIST_ENTRY ScriptEntry;
    PMINGEN_SOURCE Source;
    INT Status;
    PMINGEN_TARGET Target;
    time_t Time;
    PMINGEN_TOOL Tool;

    File = NULL;
    MakefilePath = MingenAppendPaths(Context->BuildRoot, "Makefile");
    if (MakefilePath == NULL) {
        Status = ENOMEM;
        goto CreateMakefileEnd;
    }

    if ((Context->Options & MINGEN_OPTION_VERBOSE) != 0) {
        printf("Creating %s\n", MakefilePath);
    }

    File = fopen(MakefilePath, "w");
    if (File == NULL) {
        Status = errno;
        fprintf(stderr,
                "Error: Failed to open %s: %s\n",
                MakefilePath,
                strerror(Status));

        goto CreateMakefileEnd;
    }

    Time = time(NULL);
    fprintf(File,
            "# Makefile automatically generated by Minoca mingen at %s\n",
            ctime(&Time));

    fprintf(File, "# Define high level variables\n");
    fprintf(File,
            "%s := %s\n",
            MINGEN_VARIABLE_SOURCE_ROOT,
            Context->SourceRoot);

    fprintf(File,
            "%s := %s\n",
            MINGEN_VARIABLE_BUILD_ROOT,
            Context->BuildRoot);

    fprintf(File,
            "%s := %s\n",
            MINGEN_VARIABLE_PROJECT_PATH,
            Context->ProjectFilePath);

    MingenMakePrintConfig(File, Context, NULL);
    fprintf(File, "\n# Define tools\n");
    CurrentEntry = Context->ToolList.Next;
    while (CurrentEntry != &(Context->ToolList)) {
        Tool = LIST_VALUE(CurrentEntry, MINGEN_TOOL, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if ((Tool->Flags & MINGEN_TOOL_ACTIVE) == 0) {
            continue;
        }

        fprintf(File, "TOOL_%s = ", Tool->Name);
        if (Tool->Description != NULL) {
            fprintf(File, "@echo ");
            MingenMakePrintWithVariableConversion(File, Tool->Description);
            fprintf(File, " ; \\\n    ");
        }

        MingenMakePrintWithVariableConversion(File, Tool->Command);
        fprintf(File, "\n\n");
    }

    MingenMakePrintDefaultTargets(Context, File);

    //
    // Loop over every script (file) in the build.
    //

    ScriptEntry = Context->ScriptList.Next;
    while (ScriptEntry != &(Context->ScriptList)) {
        Script = LIST_VALUE(ScriptEntry, MINGEN_SCRIPT, ListEntry);
        ScriptEntry = ScriptEntry->Next;
        if ((LIST_EMPTY(&(Script->TargetList))) ||
            ((Script->Flags & MINGEN_SCRIPT_ACTIVE) == 0)) {

            continue;
        }

        //
        // Loop over every target defined in the script.
        //

        if (Script->Path[0] == '\0') {
            fprintf(File, "# Define root targets\n");

        } else {
            fprintf(File, "# Define targets for %s\n", Script->Path);
        }

        CurrentEntry = Script->TargetList.Next;
        while (CurrentEntry != &(Script->TargetList)) {
            Target = LIST_VALUE(CurrentEntry, MINGEN_TARGET, ListEntry);
            CurrentEntry = CurrentEntry->Next;
            if ((Target->Flags & MINGEN_TARGET_ACTIVE) == 0) {
                continue;
            }

            if ((Target->Tool != NULL) &&
                (strcmp(Target->Tool, "phony") == 0)) {

                fprintf(File, ".PHONY: ");
                MingenMakePrintTargetFile(File, Context, Target);
                fprintf(File, "\n");
            }

            //
            // Add the configs for this target.
            //

            MingenMakePrintConfig(File, Context, Target);
            MingenMakePrintTargetFile(File, Context, Target);
            fprintf(File, ": ");

            //
            // Add the inputs.
            //

            for (Index = 0; Index < Target->Inputs.Count; Index += 1) {
                Input = Target->Inputs.Array[Index];
                switch (Input->Type) {
                case MingenInputTarget:
                    MingenMakePrintTargetFile(File, Context, Input);
                    break;

                case MingenInputSource:
                    Source = (PMINGEN_SOURCE)Input;
                    MingenMakePrintSource(File, Context, Source);
                    break;

                default:

                    assert(FALSE);

                    break;
                }

                if (Index + 1 != Target->Inputs.Count) {
                    fprintf(File, MINGEN_MAKE_LINE_CONTINUATION);
                }
            }

            //
            // Add the implicit and order-only inputs if there are any. Make
            // doesn't have the concept of implicit inputs, where these are
            // normal prerequisites that don't show up on the command line.
            // So lump them in with order-only prerequisites. This might cause
            // some situations where make decides not to rebuild targets it
            // should, but it's the best that can be done for these types.
            //

            if ((Target->OrderOnly.Count != 0) ||
                (Target->Implicit.Count != 0)) {

                fprintf(File, " | " MINGEN_MAKE_LINE_CONTINUATION);
                for (Index = 0;
                     Index < Target->Implicit.Count;
                     Index += 1) {

                    Input = Target->Implicit.Array[Index];
                    switch (Input->Type) {
                    case MingenInputTarget:
                        MingenMakePrintTargetFile(File, Context, Input);
                        break;

                    case MingenInputSource:
                        Source = (PMINGEN_SOURCE)Input;
                        MingenMakePrintSource(File, Context, Source);
                        break;

                    default:

                        assert(FALSE);

                        break;
                    }

                    if (Index + 1 != Target->Implicit.Count) {
                        fprintf(File, MINGEN_MAKE_LINE_CONTINUATION);
                    }
                }

                if ((Target->OrderOnly.Count != 0) &&
                    (Target->Implicit.Count != 0)) {

                    fprintf(File, MINGEN_MAKE_LINE_CONTINUATION);
                }

                for (Index = 0;
                     Index < Target->OrderOnly.Count;
                     Index += 1) {

                    Input = Target->OrderOnly.Array[Index];
                    switch (Input->Type) {
                    case MingenInputTarget:
                        MingenMakePrintTargetFile(File, Context, Input);
                        break;

                    case MingenInputSource:
                        Source = (PMINGEN_SOURCE)Input;
                        MingenMakePrintSource(File, Context, Source);
                        break;

                    default:

                        assert(FALSE);

                        break;
                    }

                    if (Index + 1 != Target->OrderOnly.Count) {
                        fprintf(File, MINGEN_MAKE_LINE_CONTINUATION);
                    }
                }
            }

            //
            // Use the tool to make the target.
            //

            if ((Target->Tool != NULL) &&
                (strcmp(Target->Tool, "phony") != 0)) {

                fprintf(File, "\n\t$(TOOL_%s)\n\n", Target->Tool);

            } else {
                fprintf(File, "\n\n");
            }
        }
    }

    MingenMakePrintBuildDirectoriesTarget(Context, File);
    if ((Context->Options & MINGEN_OPTION_NO_REBUILD_RULE) == 0) {
        MingenMakePrintMakefileTarget(Context, File);
    }

    Status = 0;

CreateMakefileEnd:
    if (File != NULL) {
        fclose(File);
    }

    if (MakefilePath != NULL) {
        free(MakefilePath);
    }

    return Status;
}

//
// --------------------------------------------------------- Internal Functions
//

VOID
MingenMakePrintDefaultTargets (
    PMINGEN_CONTEXT Context,
    FILE *File
    )

/*++

Routine Description:

    This routine emits top target statements to make the default target... the
    default.

Arguments:

    Context - Supplies a pointer to the application context.

    File - Supplies a pointer to the file to print the build directories to.

Return Value:

    None.

--*/

{

    PLIST_ENTRY CurrentEntry;
    BOOL PrintedBanner;
    PMINGEN_SCRIPT Script;
    PLIST_ENTRY ScriptEntry;
    PMINGEN_TARGET Target;

    PrintedBanner = FALSE;
    ScriptEntry = Context->ScriptList.Next;
    while (ScriptEntry != &(Context->ScriptList)) {
        Script = LIST_VALUE(ScriptEntry, MINGEN_SCRIPT, ListEntry);
        ScriptEntry = ScriptEntry->Next;
        CurrentEntry = Script->TargetList.Next;
        if ((Script->Flags & MINGEN_SCRIPT_ACTIVE) == 0) {
            continue;
        }

        while (CurrentEntry != &(Script->TargetList)) {
            Target = LIST_VALUE(CurrentEntry, MINGEN_TARGET, ListEntry);
            if (((Target->Flags & MINGEN_TARGET_DEFAULT) != 0) &&
                ((Target->Flags & MINGEN_TARGET_ACTIVE) != 0)) {

                if (PrintedBanner == FALSE) {
                    fprintf(File, "# Default target\n");
                    PrintedBanner = TRUE;
                }

                MingenMakePrintTargetFile(File, Context, Target);
                fprintf(File, ":\n");
            }

            CurrentEntry = CurrentEntry->Next;
        }
    }

    if (PrintedBanner != FALSE) {
        fprintf(File, "\n");
    }

    return;
}

VOID
MingenMakePrintBuildDirectoriesTarget (
    PMINGEN_CONTEXT Context,
    FILE *File
    )

/*++

Routine Description:

    This routine emits the built in target that ensures the directories for
    all build files exist.

Arguments:

    Context - Supplies a pointer to the application context.

    File - Supplies a pointer to the file to print the build directories to.

Return Value:

    None.

--*/

{

    UINTN Index;
    PMINGEN_PATH Path;

    fprintf(File,
            "# Built-in build directories target.\n"
            "%s:\n",
            MINGEN_BUILD_DIRECTORIES_FILE);

    for (Index = 0; Index < Context->BuildDirectories.Count; Index += 1) {
        Path = &(Context->BuildDirectories.Array[Index]);
        fprintf(File, "\t@echo \"");
        MingenMakePrintPath(File, Path);
        if (Index == 0) {
            fprintf(File, "\" > %s\n", MINGEN_BUILD_DIRECTORIES_FILE);

        } else {
            fprintf(File, "\" >> %s\n", MINGEN_BUILD_DIRECTORIES_FILE);
        }

        fprintf(File, "\tmkdir -p \"");
        MingenMakePrintPath(File, Path);
        fprintf(File, "\"\n");
    }

    fprintf(File, "\n");
    return;
}

VOID
MingenMakePrintMakefileTarget (
    PMINGEN_CONTEXT Context,
    FILE *File
    )

/*++

Routine Description:

    This routine emits the built in target that rebuilds the Makefile itself
    based on the source scripts.

Arguments:

    Context - Supplies a pointer to the application context.

    File - Supplies a pointer to the file to print the build directories to.

Return Value:

    None.

--*/

{

    PSTR BuildFileName;
    PLIST_ENTRY CurrentEntry;
    MINGEN_PATH Path;
    PMINGEN_SCRIPT Script;

    BuildFileName = Context->BuildFileName;
    if (BuildFileName == NULL) {
        BuildFileName = MINGEN_BUILD_FILE;
    }

    memset(&Path, 0, sizeof(MINGEN_PATH));
    fprintf(File, "# Built-in Makefile target.\nMakefile: ");
    CurrentEntry = Context->ScriptList.Next;
    while (CurrentEntry != &(Context->ScriptList)) {
        Script = LIST_VALUE(CurrentEntry, MINGEN_SCRIPT, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if ((Script->Flags & MINGEN_SCRIPT_ACTIVE) == 0) {
            continue;
        }

        if (strcmp(Script->CompletePath, Context->ProjectFilePath) == 0) {
            fprintf(File, MINGEN_MAKE_VARIABLE, MINGEN_VARIABLE_PROJECT_PATH);

        } else if (Script->Order == MingenScriptOrderTarget) {
            Path.Root = Script->Root;
            Path.Path = MingenAppendPaths(Script->Path, BuildFileName);
            if (Path.Path != NULL) {
                MingenMakePrintPath(File, &Path);
                free(Path.Path);
            }

        } else {
            Path.Root = Script->Root;
            Path.Path = Script->Path;
            MingenMakePrintPath(File, &Path);
        }

        if (CurrentEntry != &(Context->ScriptList)) {
            fprintf(File, MINGEN_MAKE_LINE_CONTINUATION);
        }
    }

    fprintf(File, "\n\t");
    MingenPrintRebuildCommand(Context, File);
    fprintf(File, "\n");
    return;
}

VOID
MingenMakePrintWithVariableConversion (
    FILE *File,
    PSTR String
    )

/*++

Routine Description:

    This routine prints a string to the output file, converting variable
    expressions into proper make format.

Arguments:

    File - Supplies a pointer to the file to print to.

    String - Supplies a pointer to the string to print.

Return Value:

    None.

--*/

{

    PSTR Copy;
    CHAR Original;
    PSTR Variable;

    Copy = strdup(String);
    if (Copy == NULL) {
        return;
    }

    String = Copy;
    while (*String != '\0') {
        if (*String != '$') {
            fputc(*String, File);
            String += 1;
            continue;
        }

        String += 1;

        //
        // A double dollar is just a literal dollar sign.
        //

        if (*String == '$') {
            fputc(*String, File);
            String += 1;
            continue;
        }

        //
        // A dollar sign followed by // prints the source root, and ^/ prints
        // the build root.
        //

        if ((*String == '/') && (*(String + 1) == '/')) {
            MingenMakePrintTreeRoot(File, MingenSourceTree);
            String += 2;
            continue;
        }

        if ((*String == '^') && (*(String + 1) == '/')) {
            MingenMakePrintTreeRoot(File, MingenBuildTree);
            String += 2;
            continue;
        }

        //
        // A dollar sign plus some non-variable-name character is also just
        // passed over literally.
        //

        if (!MINGEN_IS_NAME0(*String)) {
            fputc('$', File);
            fputc(*String, File);
            String += 1;
            continue;
        }

        //
        // Get to the end of the variable name.
        //

        Variable = String;
        while (MINGEN_IS_NAME(*String)) {
            String += 1;
        }

        //
        // Temporarily terminate the name, and compare it against the special
        // IN and OUT variables, which substitute differently.
        //

        Original = *String;
        *String = '\0';
        if (strcasecmp(Variable, "in") == 0) {
            fprintf(File, "%s", MINGEN_MAKE_INPUTS);

        } else if (strcasecmp(Variable, "out") == 0) {
            fprintf(File, "%s", MINGEN_MAKE_OUTPUT);

        //
        // Print the variable reference in the normal make way.
        //

        } else {
            fprintf(File, MINGEN_MAKE_VARIABLE, Variable);
        }

        *String = Original;
    }

    free(Copy);
    return;
}

VOID
MingenMakePrintTargetFile (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_TARGET Target
    )

/*++

Routine Description:

    This routine prints a target's output file name.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Target - Supplies a pointer to the target to print.

Return Value:

    None.

--*/

{

    if ((Target->Tool != NULL) &&
        (strcmp(Target->Tool, "phony") == 0)) {

        MingenMakePrintWithVariableConversion(File, Target->Output);
        return;
    }

    MingenMakePrintTreeRoot(File, Target->Tree);
    MingenMakePrintWithVariableConversion(File, Target->Output);
    return;
}

VOID
MingenMakePrintSource (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_SOURCE Source
    )

/*++

Routine Description:

    This routine prints a source's file name.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Source - Supplies a pointer to the source file information.

Return Value:

    None.

--*/

{

    MingenMakePrintTreeRoot(File, Source->Tree);
    MingenMakePrintWithVariableConversion(File, Source->Path);
    return;
}

VOID
MingenMakePrintPath (
    FILE *File,
    PMINGEN_PATH Path
    )

/*++

Routine Description:

    This routine prints a path.

Arguments:

    File - Supplies a pointer to the file to print to.

    Path - Supplies a pointer to the path to print.

Return Value:

    None.

--*/

{

    MingenMakePrintTreeRoot(File, Path->Root);
    fprintf(File, "%s", Path->Path);
    return;
}

VOID
MingenMakePrintTreeRoot (
    FILE *File,
    MINGEN_DIRECTORY_TREE Tree
    )

/*++

Routine Description:

    This routine prints the tree root shorthand for the given tree.

Arguments:

    File - Supplies a pointer to the file to print to.

    Tree - Supplies the directory tree root to print.

Return Value:

    None.

--*/

{

    switch (Tree) {
    case MingenSourceTree:
        fprintf(File, MINGEN_MAKE_VARIABLE "/", MINGEN_VARIABLE_SOURCE_ROOT);
        break;

    case MingenBuildTree:
        fprintf(File, MINGEN_MAKE_VARIABLE "/", MINGEN_VARIABLE_BUILD_ROOT);
        break;

    case MingenAbsolutePath:
        break;

    default:

        assert(FALSE);

        break;
    }

    return;
}

VOID
MingenMakePrintConfig (
    FILE *File,
    PMINGEN_CONTEXT Context,
    PMINGEN_TARGET Target
    )

/*++

Routine Description:

    This routine prints a target's configuration dictionary.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Target - Supplies a pointer to the target whose configuration should be
        printed. If NULL is supplied, then the global configuration is
        printed.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT Config;
    PLIST_ENTRY CurrentEntry;
    PCHALK_DICT_ENTRY Entry;
    INT Status;
    PCHALK_OBJECT Value;

    if (Target != NULL) {
        Config = Target->Config;

    } else {
        Config = Context->GlobalConfig;
    }

    if (Config == NULL) {
        return;
    }

    assert(Config->Header.Type == ChalkObjectDict);

    CurrentEntry = Config->Dict.EntryList.Next;
    while (CurrentEntry != &(Config->Dict.EntryList)) {
        Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        Value = Entry->Value;
        if (Entry->Key->Header.Type != ChalkObjectString) {
            fprintf(stderr,
                    "Error: Skipping config object with non-string key.\n");

            continue;
        }

        //
        // Quietly ignore nulls.
        //

        if (Value->Header.Type == ChalkObjectNull) {
            continue;
        }

        //
        // Noisily ignore other types.
        //

        if ((Value->Header.Type != ChalkObjectString) &&
            (Value->Header.Type != ChalkObjectInteger) &&
            (Value->Header.Type != ChalkObjectList)) {

            fprintf(stderr,
                    "Error: Skipping config key %s: unsupported type.\n",
                    Entry->Key->String.String);

            continue;
        }

        if (Target != NULL) {
            MingenMakePrintTargetFile(File, Context, Target);
            fprintf(File, ": ");
        }

        fprintf(File, "%s := ", Entry->Key->String.String);
        Status = MingenMakePrintConfigValue(File, Value);
        if (Status != 0) {
            fprintf(stderr,
                    "Error: Skipping some values for key %s.\n",
                    Entry->Key->String.String);
        }

        fprintf(File, "\n");
    }

    return;
}

INT
MingenMakePrintConfigValue (
    FILE *File,
    PCHALK_OBJECT Value
    )

/*++

Routine Description:

    This routine prints a configuration value.

Arguments:

    File - Supplies a pointer to the file to print to.

    Value - Supplies a pointer to the object to print.

Return Value:

    0 on success.

    -1 if some entries were skipped.

--*/

{

    UINTN Index;
    INT Status;
    INT TotalStatus;

    TotalStatus = 0;
    if (Value->Header.Type == ChalkObjectList) {

        //
        // Recurse to print every object on the list, separated by a space.
        //

        for (Index = 0; Index < Value->List.Count; Index += 1) {
            Status = MingenMakePrintConfigValue(File, Value->List.Array[Index]);
            if (Status != 0) {
                TotalStatus = Status;
            }

            if (Index != Value->List.Count - 1) {
                fprintf(File, " ");
            }
        }

    } else if (Value->Header.Type == ChalkObjectInteger) {
        fprintf(File, "%lld", Value->Integer.Value);

    } else if (Value->Header.Type == ChalkObjectString) {
        MingenMakePrintWithVariableConversion(File, Value->String.String);

    } else {
        TotalStatus = -1;
    }

    return TotalStatus;
}

