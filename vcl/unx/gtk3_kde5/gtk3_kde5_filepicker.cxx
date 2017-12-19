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

#include "gtk3_kde5_filepicker.hxx"

#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <cppuhelper/interfacecontainer.h>
#include <cppuhelper/supportsservice.hxx>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <com/sun/star/ui/dialogs/CommonFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/ExtendedFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/ControlActions.hpp>
#include <com/sun/star/ui/dialogs/ExecutableDialogResults.hpp>

#include <osl/mutex.hxx>

#include <fpicker/strings.hrc>
#include <vcl/svapp.hxx>
#include <vcl/sysdata.hxx>
#include <vcl/syswin.hxx>

#include <osl/file.h>
#include <osl/process.h>

#include "FPServiceInfo.hxx"

#undef Region

#include "unx/geninst.h"

#include "strings.hrc"

#include <future>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <unx/gtk/gtkdata.hxx>

#include <boost/filesystem/path.hpp>
#include <boost/process/environment.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/io.hpp>

using namespace ::com::sun::star;
using namespace ::com::sun::star::ui::dialogs;
using namespace ::com::sun::star::ui::dialogs::TemplateDescription;
using namespace ::com::sun::star::ui::dialogs::ExtendedFilePickerElementIds;
using namespace ::com::sun::star::ui::dialogs::CommonFilePickerElementIds;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::uno;
namespace bp = boost::process;
namespace bf = boost::filesystem;

// helper functions

namespace
{
uno::Sequence<OUString> SAL_CALL FilePicker_getSupportedServiceNames()
{
    uno::Sequence<OUString> aRet(3);
    aRet[0] = "com.sun.star.ui.dialogs.FilePicker";
    aRet[1] = "com.sun.star.ui.dialogs.SystemFilePicker";
    aRet[2] = "com.sun.star.ui.dialogs.Gtk3KDE5FilePicker";
    return aRet;
}

bf::path applicationDirPath()
{
    OUString applicationFilePath;
    osl_getExecutableFile(&applicationFilePath.pData);
    OUString applicationSystemPath;
    osl_getSystemPathFromFileURL(applicationFilePath.pData, &applicationSystemPath.pData);
    auto sysPath = applicationSystemPath.toUtf8();
    auto ret = bf::path(sysPath.getStr(), sysPath.getStr() + sysPath.getLength());
    ret.remove_filename();
    return ret;
}

bf::path findPickerExecutable()
{
    auto paths = boost::this_process::path();
    paths.insert(paths.begin(), applicationDirPath());
    auto ret = bp::search_path("lo_kde5filepicker", paths);
    if (ret.empty())
        throw bp::process_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                "could not find lo_kde5filepicker executable");
    return ret;
}
}

// Gtk3KDE5FilePicker

Gtk3KDE5FilePicker::Gtk3KDE5FilePicker(const uno::Reference<uno::XComponentContext>&)
    : Gtk3KDE5FilePicker_Base(_helperMutex)
    , m_stdout()
    , m_stdin()
    , m_process(findPickerExecutable(), bp::std_out > m_stdout, bp::std_in < m_stdin)
{
    setMultiSelectionMode(false);
}

Gtk3KDE5FilePicker::~Gtk3KDE5FilePicker()
{
    sendCommand(Commands::Quit);
    if (m_process.running())
        m_process.wait_for(std::chrono::milliseconds(100));
}

void SAL_CALL
Gtk3KDE5FilePicker::addFilePickerListener(const uno::Reference<XFilePickerListener>& xListener)
{
    SolarMutexGuard aGuard;
    m_xListener = xListener;
}

void SAL_CALL
Gtk3KDE5FilePicker::removeFilePickerListener(const uno::Reference<XFilePickerListener>&)
{
    SolarMutexGuard aGuard;
    m_xListener.clear();
}

void SAL_CALL Gtk3KDE5FilePicker::setTitle(const OUString& title)
{
    sendCommand(Commands::SetTitle, title);
}

static gboolean ignoreDeleteEvent(GtkWidget* /*widget*/, GdkEvent* /*event*/,
                                  gpointer /*user_data*/)
{
    return true;
}

std::function<void()> Gtk3KDE5FilePicker::blockMainWindow()
{
    vcl::Window* pParentWin = Application::GetDefDialogParent();
    if (!pParentWin)
        return {};

    const SystemEnvData* pSysData = static_cast<SystemWindow*>(pParentWin)->GetSystemData();
    if (!pSysData)
        return {};

    sendCommand(Commands::SetWinId, pSysData->aWindow);

    auto* pMainWindow = reinterpret_cast<GtkWidget*>(pSysData->pWidget);
    if (!pMainWindow)
        return {};

    SolarMutexGuard guard;
    auto deleteEventSignalId = g_signal_lookup("delete_event", gtk_widget_get_type());

    // disable the mainwindow
    gtk_widget_set_sensitive(pMainWindow, false);

    // block the GtkSalFrame delete_event handler
    auto blockedHandler = g_signal_handler_find(
        pMainWindow, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA),
        deleteEventSignalId, 0, nullptr, nullptr, pSysData->pSalFrame);
    g_signal_handler_block(pMainWindow, blockedHandler);

    // prevent the window from being closed
    auto ignoreDeleteEventHandler
        = g_signal_connect(pMainWindow, "delete_event", G_CALLBACK(ignoreDeleteEvent), nullptr);

    return [pMainWindow, ignoreDeleteEventHandler, blockedHandler] {
        SolarMutexGuard cleanupGuard;
        // re-enable window
        gtk_widget_set_sensitive(pMainWindow, true);

        // allow it to be closed again
        g_signal_handler_disconnect(pMainWindow, ignoreDeleteEventHandler);

        // unblock the GtkSalFrame handler
        g_signal_handler_unblock(pMainWindow, blockedHandler);
    };
}

sal_Int16 SAL_CALL Gtk3KDE5FilePicker::execute()
{
    auto restoreMainWindow = blockMainWindow();

    sendCommand(Commands::Execute);
    sal_Bool accepted = false;
    readResponse(accepted);

    if (restoreMainWindow)
        restoreMainWindow();

    return accepted ? ExecutableDialogResults::OK : ExecutableDialogResults::CANCEL;
}

void SAL_CALL Gtk3KDE5FilePicker::setMultiSelectionMode(sal_Bool multiSelect)
{
    sendCommand(Commands::SetMultiSelectionMode, multiSelect);
}

void SAL_CALL Gtk3KDE5FilePicker::setDefaultName(const OUString& name)
{
    sendCommand(Commands::SetDefaultName, name);
}

void SAL_CALL Gtk3KDE5FilePicker::setDisplayDirectory(const OUString& dir)
{
    sendCommand(Commands::SetDisplayDirectory, dir);
}

OUString SAL_CALL Gtk3KDE5FilePicker::getDisplayDirectory()
{
    sendCommand(Commands::GetDisplayDirectory);
    OUString dir;
    readResponse(dir);
    return dir;
}

uno::Sequence<OUString> SAL_CALL Gtk3KDE5FilePicker::getFiles()
{
    uno::Sequence<OUString> seq = getSelectedFiles();
    if (seq.getLength() > 1)
        seq.realloc(1);
    return seq;
}

uno::Sequence<OUString> SAL_CALL Gtk3KDE5FilePicker::getSelectedFiles()
{
    sendCommand(Commands::GetSelectedFiles);
    uno::Sequence<OUString> seq;
    readResponse(seq);
    return seq;
}

void SAL_CALL Gtk3KDE5FilePicker::appendFilter(const OUString& title, const OUString& filter)
{
    sendCommand(Commands::AppendFilter, title, filter);
}

void SAL_CALL Gtk3KDE5FilePicker::setCurrentFilter(const OUString& title)
{
    sendCommand(Commands::SetCurrentFilter, title);
}

OUString SAL_CALL Gtk3KDE5FilePicker::getCurrentFilter()
{
    sendCommand(Commands::GetCurrentFilter);
    OUString filter;
    readResponse(filter);
    return filter;
}

void SAL_CALL Gtk3KDE5FilePicker::appendFilterGroup(const OUString& /*rGroupTitle*/,
                                                    const uno::Sequence<beans::StringPair>& filters)
{
    const sal_uInt16 length = filters.getLength();
    for (sal_uInt16 i = 0; i < length; ++i)
    {
        beans::StringPair aPair = filters[i];
        appendFilter(aPair.First, aPair.Second);
    }
}

void SAL_CALL Gtk3KDE5FilePicker::setValue(sal_Int16 controlId, sal_Int16 nControlAction,
                                           const uno::Any& value)
{
    if (value.has<sal_Bool>())
    {
        sendCommand(Commands::SetValue, controlId, nControlAction, value.get<sal_Bool>());
    }
    else
    {
        OSL_TRACE("set value of unhandled type %d", controlId);
    }
}

uno::Any SAL_CALL Gtk3KDE5FilePicker::getValue(sal_Int16 controlId, sal_Int16 nControlAction)
{
    if (CHECKBOX_AUTOEXTENSION == controlId)
        // We ignore this one and rely on QFileDialog to provide the function.
        // Always return false, to pretend we do not support this, otherwise
        // LO core would try to be smart and cut the extension in some places,
        // interfering with QFileDialog's handling of it. QFileDialog also
        // saves the value of the setting, so LO core is not needed for that either.
        return uno::Any(false);

    sendCommand(Commands::GetValue, controlId, nControlAction);

    sal_Bool value = false;
    readResponse(value);

    return uno::Any(value);
}

void SAL_CALL Gtk3KDE5FilePicker::enableControl(sal_Int16 controlId, sal_Bool enable)
{
    sendCommand(Commands::EnableControl, controlId, enable);
}

void SAL_CALL Gtk3KDE5FilePicker::setLabel(sal_Int16 controlId, const OUString& label)
{
    sendCommand(Commands::SetLabel, controlId, label);
}

OUString SAL_CALL Gtk3KDE5FilePicker::getLabel(sal_Int16 controlId)
{
    sendCommand(Commands::GetLabel, controlId);
    OUString label;
    readResponse(label);
    return label;
}

static OUString getResString(const char* pResId)
{
    if (pResId == nullptr)
        return {};

    return VclResId(pResId);
}

void Gtk3KDE5FilePicker::addCustomControl(sal_Int16 controlId)
{
    const char* resId = nullptr;

    switch (controlId)
    {
        case CHECKBOX_AUTOEXTENSION:
            resId = STR_FPICKER_AUTO_EXTENSION;
            break;
        case CHECKBOX_PASSWORD:
            resId = STR_FPICKER_PASSWORD;
            break;
        case CHECKBOX_FILTEROPTIONS:
            resId = STR_FPICKER_FILTER_OPTIONS;
            break;
        case CHECKBOX_READONLY:
            resId = STR_FPICKER_READONLY;
            break;
        case CHECKBOX_LINK:
            resId = STR_FPICKER_INSERT_AS_LINK;
            break;
        case CHECKBOX_PREVIEW:
            resId = STR_FPICKER_SHOW_PREVIEW;
            break;
        case CHECKBOX_SELECTION:
            resId = STR_FPICKER_SELECTION;
            break;
        case PUSHBUTTON_PLAY:
            resId = STR_FPICKER_PLAY;
            break;
        case LISTBOX_VERSION:
            resId = STR_FPICKER_VERSION;
            break;
        case LISTBOX_TEMPLATE:
            resId = STR_FPICKER_TEMPLATES;
            break;
        case LISTBOX_IMAGE_TEMPLATE:
            resId = STR_FPICKER_IMAGE_TEMPLATE;
            break;
        case LISTBOX_VERSION_LABEL:
        case LISTBOX_TEMPLATE_LABEL:
        case LISTBOX_IMAGE_TEMPLATE_LABEL:
        case LISTBOX_FILTER_SELECTOR:
            break;
    }

    switch (controlId)
    {
        case CHECKBOX_AUTOEXTENSION:
        case CHECKBOX_PASSWORD:
        case CHECKBOX_FILTEROPTIONS:
        case CHECKBOX_READONLY:
        case CHECKBOX_LINK:
        case CHECKBOX_PREVIEW:
        case CHECKBOX_SELECTION:
        {
            // the checkbox is created even for CHECKBOX_AUTOEXTENSION to simplify
            // code, but the checkbox is hidden and ignored
            sal_Bool hidden = controlId == CHECKBOX_AUTOEXTENSION;

            sendCommand(Commands::AddCheckBox, controlId, hidden, getResString(resId));

            break;
        }
        case PUSHBUTTON_PLAY:
        case LISTBOX_VERSION:
        case LISTBOX_TEMPLATE:
        case LISTBOX_IMAGE_TEMPLATE:
        case LISTBOX_VERSION_LABEL:
        case LISTBOX_TEMPLATE_LABEL:
        case LISTBOX_IMAGE_TEMPLATE_LABEL:
        case LISTBOX_FILTER_SELECTOR:
            break;
    }
}

void SAL_CALL Gtk3KDE5FilePicker::initialize(const uno::Sequence<uno::Any>& args)
{
    // parameter checking
    uno::Any arg;
    if (args.getLength() == 0)
    {
        throw lang::IllegalArgumentException(OUString("no arguments"),
                                             static_cast<XFilePicker2*>(this), 1);
    }

    arg = args[0];

    if ((arg.getValueType() != cppu::UnoType<sal_Int16>::get())
        && (arg.getValueType() != cppu::UnoType<sal_Int8>::get()))
    {
        throw lang::IllegalArgumentException(OUString("invalid argument type"),
                                             static_cast<XFilePicker2*>(this), 1);
    }

    sal_Int16 templateId = -1;
    arg >>= templateId;

    sal_Bool saveDialog = false;
    switch (templateId)
    {
        case FILEOPEN_SIMPLE:
            break;

        case FILESAVE_SIMPLE:
            saveDialog = true;
            break;

        case FILESAVE_AUTOEXTENSION:
            saveDialog = true;
            addCustomControl(CHECKBOX_AUTOEXTENSION);
            break;

        case FILESAVE_AUTOEXTENSION_PASSWORD:
        {
            saveDialog = true;
            addCustomControl(CHECKBOX_PASSWORD);
            break;
        }
        case FILESAVE_AUTOEXTENSION_PASSWORD_FILTEROPTIONS:
        {
            saveDialog = true;
            addCustomControl(CHECKBOX_AUTOEXTENSION);
            addCustomControl(CHECKBOX_PASSWORD);
            addCustomControl(CHECKBOX_FILTEROPTIONS);
            break;
        }
        case FILESAVE_AUTOEXTENSION_SELECTION:
            saveDialog = true;
            addCustomControl(CHECKBOX_AUTOEXTENSION);
            addCustomControl(CHECKBOX_SELECTION);
            break;

        case FILESAVE_AUTOEXTENSION_TEMPLATE:
            saveDialog = true;
            addCustomControl(CHECKBOX_AUTOEXTENSION);
            addCustomControl(LISTBOX_TEMPLATE);
            break;

        case FILEOPEN_LINK_PREVIEW_IMAGE_TEMPLATE:
            addCustomControl(CHECKBOX_LINK);
            addCustomControl(CHECKBOX_PREVIEW);
            addCustomControl(LISTBOX_IMAGE_TEMPLATE);
            break;

        case FILEOPEN_PLAY:
            addCustomControl(PUSHBUTTON_PLAY);
            break;

        case FILEOPEN_READONLY_VERSION:
            addCustomControl(CHECKBOX_READONLY);
            addCustomControl(LISTBOX_VERSION);
            break;

        case FILEOPEN_LINK_PREVIEW:
            addCustomControl(CHECKBOX_LINK);
            addCustomControl(CHECKBOX_PREVIEW);
            break;

        default:
            OSL_TRACE("Unknown templates %d", templateId);
            return;
    }

    setTitle(getResString(saveDialog ? STR_FPICKER_SAVE : STR_FPICKER_OPEN));

    sendCommand(Commands::Initialize, saveDialog);
}

void SAL_CALL Gtk3KDE5FilePicker::cancel() {}

void SAL_CALL Gtk3KDE5FilePicker::disposing(const lang::EventObject& rEvent)
{
    uno::Reference<XFilePickerListener> xFilePickerListener(rEvent.Source, uno::UNO_QUERY);

    if (xFilePickerListener.is())
    {
        removeFilePickerListener(xFilePickerListener);
    }
}

OUString SAL_CALL Gtk3KDE5FilePicker::getImplementationName()
{
    return OUString(FILE_PICKER_IMPL_NAME);
}

sal_Bool SAL_CALL Gtk3KDE5FilePicker::supportsService(const OUString& ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

uno::Sequence<OUString> SAL_CALL Gtk3KDE5FilePicker::getSupportedServiceNames()
{
    return FilePicker_getSupportedServiceNames();
}

void Gtk3KDE5FilePicker::filterChanged()
{
    FilePickerEvent aEvent;
    aEvent.ElementId = LISTBOX_FILTER;
    OSL_TRACE("filter changed");
    if (m_xListener.is())
        m_xListener->controlStateChanged(aEvent);
}

void Gtk3KDE5FilePicker::selectionChanged()
{
    FilePickerEvent aEvent;
    OSL_TRACE("file selection changed");
    if (m_xListener.is())
        m_xListener->fileSelectionChanged(aEvent);
}

void readIpcArg(std::istream& stream, OUString& str)
{
    const auto buffer = readIpcStringArg(stream);
    str = OUString::fromUtf8(OString(buffer.data(), buffer.size()));
}

void readIpcArg(std::istream& stream, uno::Sequence<OUString>& seq)
{
    uint32_t numFiles = 0;
    stream >> numFiles;
    stream.ignore(); // skip space;
    seq.realloc(numFiles);
    for (size_t i = 0; i < numFiles; ++i)
    {
        readIpcArg(stream, seq[i]);
    }
}

void sendIpcArg(std::ostream& stream, const OUString& string)
{
    const auto utf8 = string.toUtf8();
    sendIpcStringArg(stream, utf8.getLength(), utf8.getStr());
}

template <typename... Args>
void Gtk3KDE5FilePicker::sendCommand(Commands command, const Args&... args)
{
    sendIpcArgs(m_stdin, command, args...);
}

template <typename... Args> void Gtk3KDE5FilePicker::readResponse(Args&... args)
{
    // read synchronously from a background thread and run the eventloop until the value becomes available
    // this allows us to keep the GUI responsive and also enables access to the LO clipboard
    auto valuesRead = std::async(std::launch::async, [&]() { readIpcArgs(m_stdout, args...); });
    while (valuesRead.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
    {
        GetGtkSalData()->Yield(false, true);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
