/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <QtWidgets/QApplication>

#include "Gtk3KDE5FilePicker.hxx"

void InitQt5();
void DeInitQt5();

#include "../gtk3/gtk3gtkinst.cxx"

// FIXME: put into separate file / instance, share code with Qt5Instance
void InitQt5()
{
    char** pFakeArgvFreeable = nullptr;

    int nFakeArgc = 2;
    const sal_uInt32 nParams = osl_getCommandArgCount();
    OString aDisplay;
    OUString aParam, aBin;

    for (sal_uInt32 nIdx = 0; nIdx < nParams; ++nIdx)
    {
        osl_getCommandArg(nIdx, &aParam.pData);
        if (aParam != "-display")
            continue;
        if (!pFakeArgvFreeable)
        {
            pFakeArgvFreeable = new char*[nFakeArgc + 2];
            pFakeArgvFreeable[nFakeArgc++] = strdup("-display");
        }
        else
            free(pFakeArgvFreeable[nFakeArgc]);

        ++nIdx;
        osl_getCommandArg(nIdx, &aParam.pData);
        aDisplay = OUStringToOString(aParam, osl_getThreadTextEncoding());
        pFakeArgvFreeable[nFakeArgc] = strdup(aDisplay.getStr());
    }
    if (!pFakeArgvFreeable)
        pFakeArgvFreeable = new char*[nFakeArgc];
    else
        nFakeArgc++;

    osl_getExecutableFile(&aParam.pData);
    osl_getSystemPathFromFileURL(aParam.pData, &aBin.pData);
    OString aExec = OUStringToOString(aBin, osl_getThreadTextEncoding());
    pFakeArgvFreeable[0] = strdup(aExec.getStr());
    pFakeArgvFreeable[1] = strdup("--nocrashhandler");

    char** pFakeArgv = new char*[nFakeArgc];
    for (int i = 0; i < nFakeArgc; i++)
        pFakeArgv[i] = pFakeArgvFreeable[i];

    char* session_manager = nullptr;
    if (getenv("SESSION_MANAGER") != nullptr)
    {
        session_manager = strdup(getenv("SESSION_MANAGER"));
        unsetenv("SESSION_MANAGER");
    }

    int* pFakeArgc = new int;
    *pFakeArgc = nFakeArgc;
    new QApplication(*pFakeArgc, pFakeArgv);

    if (session_manager != nullptr)
    {
        // coverity[tainted_string] - trusted source for setenv
        setenv("SESSION_MANAGER", session_manager, 1);
        free(session_manager);
    }
}

void DeInitQt5()
{
    qApp->quit();
    delete qApp;
}

uno::Reference<ui::dialogs::XFilePicker2>
GtkInstance::createFilePicker(const uno::Reference<uno::XComponentContext>& xMSF)
{
    return uno::Reference<ui::dialogs::XFilePicker2>(new Gtk3KDE5FilePicker(xMSF));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
