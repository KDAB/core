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

#pragma once

#include <cppuhelper/compbase.hxx>

#include <osl/conditn.hxx>
#include <osl/mutex.hxx>

#include <rtl/ustrbuf.hxx>

#include <boost/process/child.hpp>
#include <boost/process/pipe.hpp>

#include "filepicker_ipc_commands.hxx"

#include <functional>
#include <future>

void readIpcArg(std::istream& stream, OUString& str);

void readIpcArg(std::istream& stream, css::uno::Sequence<OUString>& seq);

void sendIpcArg(std::ostream& stream, const OUString& string);

OUString getResString(const char* pResId);

class Gtk3KDE5FilePickerIpc
{
protected:
    boost::process::ipstream m_stdout;
    boost::process::opstream m_stdin;
    boost::process::child m_process;

public:
    explicit Gtk3KDE5FilePickerIpc();
    ~Gtk3KDE5FilePickerIpc();

    sal_Int16 SAL_CALL execute();

    template <typename... Args>
    void sendCommand(Commands command, const Args&... args)
    {
        sendIpcArgs(m_stdin, command, args...);
    }

    template <typename... Args>
    void readResponse(Args&... args)
    {
        // read synchronously from a background thread and run the eventloop until the value becomes available
        // this allows us to keep the GUI responsive and also enables access to the LO clipboard
        await(std::async(std::launch::async, [&]() { readIpcArgs(m_stdout, args...); }));
    }

private:
    std::function<void()> SAL_CALL blockMainWindow();

    static void SAL_CALL await(const std::future<void>& future);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
