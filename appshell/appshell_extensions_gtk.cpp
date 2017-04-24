/*
 * Copyright (c) 2012 Chhatoi Pritam Baral <pritam@pritambaral.com>. All rights reserved.
 *  
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *  
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 * 
 */

#include "client_app.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include "appshell_extensions.h"
#include "native_menu_model.h"
#include "client_handler.h"

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <sstream>
#include <vector>

extern CefRefPtr<ClientHandler> g_handler;

// Supported browsers (order matters):
//   - google-chorme 
//   - chromium-browser - chromium executable name (in ubuntu)
//   - chromium - other chromium executable name (in arch linux)
std::string browsers[3] = {"google-chrome", "chromium-browser", "chromium"};

int ConvertLinuxErrorCode(int errorCode, bool isReading = true);
int ConvertGnomeErrorCode(GError* gerror, bool isReading = true);

extern bool isReallyClosing;

int GErrorToErrorCode(GError *gerror) {
    int error = ConvertGnomeErrorCode(gerror);
    
    // uncomment to see errors printed to the console
    //g_warning(gerror->message);
    g_error_free(gerror);
    
    return error;
}

int32 OpenLiveBrowser(ExtensionString argURL, bool enableRemoteDebugging)
{
    const char *remoteDebuggingFormat = "--no-first-run --no-default-browser-check --allow-file-access-from-files --temp-profile --user-data-dir=%s --remote-debugging-port=9222";
    gchar *remoteDebugging;
    gchar *cmdline;
    int error = ERR_BROWSER_NOT_INSTALLED;
    GError *gerror = NULL;
    
    if (enableRemoteDebugging) {
        CefString appSupportDirectory = ClientApp::AppGetSupportDirectory();

        // TODO: (INGO) to better understand to string conversion issue, I need a consultant
        // here. Getting the char* from CefString I had to call ToString().c_str()
        // Calling only c_str() didn't return anything.
        gchar *userDataDir = g_strdup_printf("%s/live-dev-profile",
                                        appSupportDirectory.ToString().c_str());  
        g_message("USERDATADIR= %s", userDataDir);
        remoteDebugging = g_strdup_printf(remoteDebuggingFormat, userDataDir);
        
        g_free(userDataDir);
    } else {
        remoteDebugging = g_strdup("");
    }

    // check for supported browsers (in PATH directories)
    for (size_t i = 0; i < sizeof(browsers) / sizeof(browsers[0]); i++) {
        cmdline = g_strdup_printf("%s %s %s", browsers[i].c_str(), argURL.c_str(), remoteDebugging);

        if (g_spawn_command_line_async(cmdline, &gerror)) {
            // browser is found in os; stop iterating
            error = NO_ERROR;
        } else {
            error = ConvertGnomeErrorCode(gerror);
        }

        g_free(cmdline);
        
        if (error == NO_ERROR) {
            break;
        } else {
            g_error_free(gerror);
            gerror = NULL;
        }
    }
    
    g_free(remoteDebugging);

    return error;
}

void CloseLiveBrowser(CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> response)
{
    const char *killall = "killall -9 %s";
    gchar *cmdline;
    gint exitstatus;
    GError *gerror = NULL;
    int error = NO_ERROR;
    CefRefPtr<CefListValue> responseArgs = response->GetArgumentList();

    // check for supported browsers (in PATH directories)
    for (size_t i = 0; i < sizeof(browsers) / sizeof(browsers[0]); i++) {
        cmdline = g_strdup_printf(killall, browsers[i].c_str());

        // FIXME (jasonsanjose): use async
        if (!g_spawn_command_line_sync(cmdline, NULL, NULL, &exitstatus, &gerror)) {
            error = ConvertGnomeErrorCode(gerror);
            g_error_free(gerror);
        }

        g_free(cmdline);

        // browser is found in os; stop iterating
        if (exitstatus == 0) {
            error = NO_ERROR;
            break;
        }
    }

    responseArgs->SetInt(1, error);
    browser->SendProcessMessage(PID_RENDERER, response);
}

int32 OpenURLInDefaultBrowser(ExtensionString url)
{
    GError* error = NULL;
    gtk_show_uri(NULL, url.c_str(), GDK_CURRENT_TIME, &error);
    g_error_free(error);
    return NO_ERROR;
}

int32 IsNetworkDrive(ExtensionString path, bool& isRemote)
{
    return NO_ERROR;
}

int32 ShowOpenDialog(bool allowMultipleSelection,
                     bool chooseDirectory,
                     ExtensionString title,
                     ExtensionString initialDirectory,
                     ExtensionString fileTypes,
                     CefRefPtr<CefListValue>& selectedFiles)
{
    GtkWidget *dialog;
    const char* dialog_title = chooseDirectory ? "Open Directory" : "Open File";
    GtkFileChooserAction file_or_directory = chooseDirectory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN ;
    dialog = gtk_file_chooser_dialog_new (dialog_title,
                  NULL,
                  file_or_directory,
                  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                  GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                  NULL);
    
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename;
        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        selectedFiles->SetString(0,filename);
        g_free (filename);
    }
    
    gtk_widget_destroy (dialog);
    return NO_ERROR;
}

int32 ShowSaveDialog(ExtensionString title,
                     ExtensionString initialDirectory,
                     ExtensionString proposedNewFilename,
                     ExtensionString& newFilePath)
{
    GtkWidget *openSaveDialog;
    
    openSaveDialog = gtk_file_chooser_dialog_new(title.c_str(),
        NULL,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);
    
    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (openSaveDialog), TRUE);	
    if (!initialDirectory.empty())
    {
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (openSaveDialog), proposedNewFilename.c_str());
        
        ExtensionString folderURI = std::string("file:///") + initialDirectory;
        gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (openSaveDialog), folderURI.c_str());
    }
    
    if (gtk_dialog_run (GTK_DIALOG (openSaveDialog)) == GTK_RESPONSE_ACCEPT)
    {
        char* filePath;
        filePath = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (openSaveDialog));
        newFilePath = filePath;
        g_free (filePath);
    }
    
    gtk_widget_destroy (openSaveDialog);
    return NO_ERROR;
}

int32 ReadDir(ExtensionString path, CefRefPtr<CefListValue>& directoryContents)
{
    //# Add trailing /slash if neccessary
    if (path.length() && path[path.length() - 1] != '/')
        path += '/';

    DIR *dp;
    struct dirent *files;
    struct stat statbuf;
    ExtensionString curFile;
    
    /*struct dirent
    {
        unsigned char  d_type; #not supported on all systems, faster than stat
        char d_name[]
    }*/
    

    if((dp=opendir(path.c_str()))==NULL)
        return ConvertLinuxErrorCode(errno,true);

    std::vector<ExtensionString> resultFiles;
    std::vector<ExtensionString> resultDirs;

    while((files=readdir(dp))!=NULL)
    {
        if(!strcmp(files->d_name,".") || !strcmp(files->d_name,".."))
            continue;
        
        if(files->d_type==DT_DIR)
            resultDirs.push_back(ExtensionString(files->d_name));
        else if(files->d_type==DT_REG)
            resultFiles.push_back(ExtensionString(files->d_name));
        else
        {
            // Some file systems do not support d_type we use
            // for faster type detection. So on these file systems
            // we may get DT_UNKNOWN for all file entries, but just  
            // to be safe we will use slower stat call for all 
            // file entries that are not DT_DIR or DT_REG.
            curFile = path + files->d_name;
            if(stat(curFile.c_str(), &statbuf) == -1)
                continue;
        
            if(S_ISDIR(statbuf.st_mode))
                resultDirs.push_back(ExtensionString(files->d_name));
            else if(S_ISREG(statbuf.st_mode))
                resultFiles.push_back(ExtensionString(files->d_name));
        }
    }

    closedir(dp);

    //# List dirs first, files next
    size_t i, total = 0;
    for (i = 0; i < resultDirs.size(); i++)
        directoryContents->SetString(total++, resultDirs[i]);
    for (i = 0; i < resultFiles.size(); i++)
        directoryContents->SetString(total++, resultFiles[i]);

    return NO_ERROR;
}

int32 MakeDir(ExtensionString path, int mode)
{
    const char *pathStr = path.c_str();
    GFile *file;
    GError *gerror = NULL;
    int32 error = NO_ERROR;

    if (g_file_test(pathStr, G_FILE_TEST_EXISTS)) {
        return ERR_FILE_EXISTS;
    }

    file = g_file_new_for_path(pathStr);
    mode = mode | 0777;

    if (!g_file_make_directory(file, NULL, &gerror)) {
        error = GErrorToErrorCode(gerror);
    }
    g_object_unref(file);

    return error;
}

int Rename(ExtensionString oldName, ExtensionString newName)
{
    const char *newNameStr = newName.c_str();

    if (g_file_test(newNameStr, G_FILE_TEST_EXISTS)) {
        return ERR_FILE_EXISTS;
    }

    if (rename(oldName.c_str(), newNameStr) == -1) {
        return ConvertLinuxErrorCode(errno);
    }
}

int GetFileInfo(ExtensionString filename, uint32& modtime, bool& isDir, double& size, ExtensionString& realPath)
{
    struct stat buf;
    if(stat(filename.c_str(),&buf)==-1)
        return ConvertLinuxErrorCode(errno);

    modtime = buf.st_mtime;
    isDir = S_ISDIR(buf.st_mode);
    size = (double)buf.st_size;
    
    // TODO: Implement realPath. If "filename" is a symlink, realPath should be the actual path
    // to the linked object.
    realPath = "";
    
    return NO_ERROR;
}

const int utf8_BOM_Len = 3;
const int utf16_BOM_Len = 2;
const int utf32_BOM_Len = 4;

bool has_utf8_BOM(gchar* data, gsize length)
{
    return ((length >= utf8_BOM_Len) &&
                (data[0] == (gchar)0xEF) && (data[1] == (gchar)0xBB) && (data[2] == (gchar)0xBF));
}

bool has_utf16be_BOM(gchar* data, gsize length)
{
    return ((length >= utf16_BOM_Len) && (data[0] == (gchar)0xFE) && (data[1] == (gchar)0xFF));
}

bool has_utf16le_BOM(gchar* data, gsize length)
{
    return ((length >= utf16_BOM_Len) && (data[0] == (gchar)0xFF) && (data[1] == (gchar)0xFE));
}

bool has_utf32be_BOM(gchar* data, gsize length)
{
    return ((length >=  utf32_BOM_Len) &&
             (data[0] == (gchar)0x00) && (data[1] == (gchar)0x00) &&
             (data[2] == (gchar)0xFE) && (data[3] == (gchar)0xFF));
}

bool has_utf32le_BOM(gchar* data, gsize length)
{
   return ((length >=  utf32_BOM_Len) &&
             (data[0] == (gchar)0xFE) && (data[1] == (gchar)0xFF) &&
             (data[2] == (gchar)0x00) && (data[3] == (gchar)0x00));
}


bool has_utf16_32_BOM(gchar* data, gsize length) 
{
    return (has_utf32be_BOM(data ,length) ||
            has_utf32le_BOM(data ,length) ||
            has_utf16be_BOM(data ,length) ||
            has_utf16le_BOM(data ,length) );
}


int ReadFile(ExtensionString filename, ExtensionString encoding, std::string& contents)
{
    if (encoding != "utf8") {
        return ERR_UNSUPPORTED_ENCODING;
    }

    int error = NO_ERROR;
    GError *gerror = NULL;
    gchar *file_get_contents = NULL;
    gsize len = 0;
    
    if (!g_file_get_contents(filename.c_str(), &file_get_contents, &len, &gerror)) {
        error = GErrorToErrorCode(gerror);
        if (error == ERR_NOT_FILE) {
            error = ERR_CANT_READ;
        }
    } else {
        if (has_utf16_32_BOM(file_get_contents, len)) {
            error = ERR_UNSUPPORTED_ENCODING;
        } else  if (has_utf8_BOM(file_get_contents, len)) {
            contents.assign(file_get_contents + utf8_BOM_Len, len);        
        } else if (!g_locale_to_utf8(file_get_contents, -1, NULL, NULL, &gerror)) {
            error = ERR_UNSUPPORTED_ENCODING;
        } else {
            contents.assign(file_get_contents, len);
        }
        g_free(file_get_contents);
    }

    return error;
}



int32 WriteFile(ExtensionString filename, std::string contents, ExtensionString encoding)
{
    const char *filenameStr = filename.c_str();    
    int error = NO_ERROR;
    GError *gerror = NULL;

    if (encoding != "utf8") {
        return ERR_UNSUPPORTED_ENCODING;
    } else if (g_file_test(filenameStr, G_FILE_TEST_EXISTS) && g_access(filenameStr, W_OK) == -1) {
        return ERR_CANT_WRITE;
    }
    
    FILE* file = fopen(filenameStr, "w");
    if (file) {
        size_t size = fwrite(contents.c_str(), sizeof(gchar), contents.length(), file);
        if (size != contents.length()) {
            error = ERR_CANT_WRITE;
        }

        fclose(file);
    } else {
        return ConvertLinuxErrorCode(errno);
    }

    return error;
}

int SetPosixPermissions(ExtensionString filename, int32 mode)
{
    if (chmod(filename.c_str(),mode) == -1) {
        return ConvertLinuxErrorCode(errno);
    }

    return NO_ERROR;
}

int _deleteFile(GFile *file)
{
    int error = NO_ERROR;
    GError *gerror = NULL;

    if (!g_file_delete(file, NULL, &gerror)) {
        error = GErrorToErrorCode(gerror);
    }

    return error;
}

int _doDeleteFileOrDirectory(GFile *file)
{
    GFileEnumerator *enumerator;
    GFileInfo *fileinfo;
    int error = NO_ERROR;
    GFile *child;
    
    // deletes a file or an empty directory
    error = _deleteFile(file);
    if (error == ERR_NOT_FOUND || error == NO_ERROR) {
        return error;
    }
    
    enumerator = g_file_enumerate_children(file, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);

    if (enumerator == NULL) {
        error = ERR_UNKNOWN;
    } else {
        // recursively delete directory contents
        while ((fileinfo = g_file_enumerator_next_file(enumerator, NULL, NULL)) != NULL) {
            child = g_file_get_child(file, g_file_info_get_name(fileinfo));
            error = _doDeleteFileOrDirectory(child);
            g_object_unref(child);

            if (error != NO_ERROR) {
                break;
            }
        }

        // directory is now empty, delete it
        if (error == NO_ERROR) {
            error = _deleteFile(file);
        }

        g_object_unref(enumerator);
    }

    return error;
}

int DeleteFileOrDirectory(ExtensionString filename)
{
    GFile *file = g_file_new_for_path(filename.c_str());
    int error = _doDeleteFileOrDirectory(file);
    g_object_unref(file);

    return error;
}

void MoveFileOrDirectoryToTrash(ExtensionString filename, CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> response)
{
    int error = NO_ERROR;
    GFile *file = g_file_new_for_path(filename.c_str());
    GError *gerror = NULL;
    if (!g_file_trash(file, NULL, &gerror)) {
        error = ConvertGnomeErrorCode(gerror);
        g_error_free(gerror);
    }
    g_object_unref(file);
    
    response->GetArgumentList()->SetInt(1, error);
    browser->SendProcessMessage(PID_RENDERER, response);
}

void CloseWindow(CefRefPtr<CefBrowser> browser)
{
    if (browser.get()) {
        isReallyClosing = true;
        // //# Hack because CEF's CloseBrowser() is bad. Should emit delete_event instead of directly destroying widget
        // GtkWidget* hwnd = gtk_widget_get_toplevel (browser->GetHost()->GetWindowHandle() );
        // if(gtk_widget_is_toplevel (hwnd))
        //     gtk_signal_emit_by_name(GTK_OBJECT(hwnd), "delete_event");
        // else
            browser->GetHost()->CloseBrowser(true);
    }
}

void BringBrowserWindowToFront(CefRefPtr<CefBrowser> browser)
{
    if (browser.get()) {
        GtkWindow* hwnd = GTK_WINDOW(browser->GetHost()->GetWindowHandle());
        if (hwnd) 
            gtk_window_present(hwnd);
    }
}

int ShowFolderInOSWindow(ExtensionString pathname)
{
    int error = NO_ERROR;
    GError *gerror = NULL;
    gchar *uri = g_strdup_printf("file://%s", pathname.c_str());
    
    if (!gtk_show_uri(NULL, uri, GDK_CURRENT_TIME, &gerror)) {
        error = ConvertGnomeErrorCode(gerror);
        g_warning("%s", gerror->message);
        g_error_free(gerror);
    }
    
    g_free(uri);

    return error;
}


int ConvertGnomeErrorCode(GError* gerror, bool isReading) 
{
    if (gerror == NULL) 
        return NO_ERROR;

    if (gerror->domain == G_FILE_ERROR) {
        switch(gerror->code) {
        case G_FILE_ERROR_EXIST:
            return ERR_FILE_EXISTS;
        case G_FILE_ERROR_NOTDIR:
            return ERR_NOT_DIRECTORY;
        case G_FILE_ERROR_ISDIR:
            return ERR_NOT_FILE;
        case G_FILE_ERROR_NXIO:
        case G_FILE_ERROR_NOENT:
            return ERR_NOT_FOUND;
        case G_FILE_ERROR_NOSPC:
            return ERR_OUT_OF_SPACE;
        case G_FILE_ERROR_INVAL:
            return ERR_INVALID_PARAMS;
        case G_FILE_ERROR_ROFS:
            return ERR_CANT_WRITE;
        case G_FILE_ERROR_BADF:
        case G_FILE_ERROR_ACCES:
        case G_FILE_ERROR_PERM:
        case G_FILE_ERROR_IO:
           return isReading ? ERR_CANT_READ : ERR_CANT_WRITE;
        default:
            return ERR_UNKNOWN;
        }  
    } else if (gerror->domain == G_IO_ERROR) {
        switch(gerror->code) {
        case G_IO_ERROR_NOT_FOUND:
            return ERR_NOT_FOUND;
        case G_IO_ERROR_NO_SPACE:
            return ERR_OUT_OF_SPACE;
        case G_IO_ERROR_INVALID_ARGUMENT:
            return ERR_INVALID_PARAMS;
        default:
            return ERR_UNKNOWN;
        }
    }
}

int ConvertLinuxErrorCode(int errorCode, bool isReading)
{
//    printf("LINUX ERROR! %d %s\n", errorCode, strerror(errorCode));
    switch (errorCode) {
    case NO_ERROR:
        return NO_ERROR;
    case ENOENT:
        return ERR_NOT_FOUND;
    case EACCES:
        return isReading ? ERR_CANT_READ : ERR_CANT_WRITE;
    case ENOTDIR:
        return ERR_NOT_DIRECTORY;
//    case ERROR_WRITE_PROTECT:
//        return ERR_CANT_WRITE;
//    case ERROR_HANDLE_DISK_FULL:
//        return ERR_OUT_OF_SPACE;
//    case ERROR_ALREADY_EXISTS:
//        return ERR_FILE_EXISTS;
    default:
        return ERR_UNKNOWN;
    }
}

int32 CopyFile(ExtensionString src, ExtensionString dest)
{
    int error = NO_ERROR;
    GFile *source = g_file_new_for_path(src.c_str());
    GFile *destination = g_file_new_for_path(dest.c_str());
    GError *gerror = NULL;

    if (!g_file_copy(source, destination, (GFileCopyFlags)(G_FILE_COPY_OVERWRITE|G_FILE_COPY_NOFOLLOW_SYMLINKS|G_FILE_COPY_TARGET_DEFAULT_PERMS), NULL, NULL, NULL, &gerror)) {
        error = ConvertGnomeErrorCode(gerror);
        g_error_free(gerror);
    }
    g_object_unref(source);
    g_object_unref(destination);

    return error;    
}

int32 GetPendingFilesToOpen(ExtensionString& files)
{
}

static GtkWidget* GetMenuBar(CefRefPtr<CefBrowser> browser)
{
    GtkWidget* window = (GtkWidget*)getMenuParent(browser);
    GtkWidget* widget;
    GList *children, *iter;
    GtkWidget* menuBar = NULL;

    children = gtk_container_get_children(GTK_CONTAINER(window));
    for(iter = children; iter != NULL; iter = g_list_next(iter)) {
        widget = (GtkWidget*)iter->data;

        if (GTK_IS_MENU_BAR(widget)) {
            menuBar = widget;
            break;
        }
    }

    g_list_free(children);

    return menuBar;
}

static int GetPosition(const ExtensionString& positionString, const ExtensionString& relativeId,
                       GtkWidget* container, NativeMenuModel& model)
{
    if (positionString == "" || positionString == "last")
        return -1;
    else if (positionString == "first")
        return 0;
    else if (relativeId.size() > 0) {
        int relativeTag = model.getTag(relativeId);
        GtkWidget* relativeMenuItem = (GtkWidget*) model.getOsItem(relativeTag);
        int position = 0;
        GList* children = gtk_container_get_children(GTK_CONTAINER(container));
        GList* iter;
        for (iter = children; iter != NULL; iter = g_list_next(iter)) {
            if (iter->data == relativeMenuItem)
                break;
            position++;
        }
        if (iter == NULL)
            position = -1;
        else if (positionString == "before")
            ;
        else if (positionString == "after")
            position++;
        else if (positionString == "firstInSection") {
            for (; iter != NULL; iter = g_list_previous(iter)) {
                if (GTK_IS_SEPARATOR_MENU_ITEM(iter->data))
                    break;
                position--;
            }
            position++;
        }
        else if (positionString == "lastInSection") {
            for (; iter != NULL; iter = g_list_next(iter)) {
                if (GTK_IS_SEPARATOR_MENU_ITEM(iter->data))
                    break;
                position++;
            }
        }
        else
            position = -1;
        g_list_free(children);
        return position;
    }
    else
        return -1;
}

int32 AddMenu(CefRefPtr<CefBrowser> browser, ExtensionString title, ExtensionString command,
              ExtensionString positionString, ExtensionString relativeId)
{
    NativeMenuModel& model = NativeMenuModel::getInstance(getMenuParent(browser));
    int tag = model.getTag(command);
    if (tag == kTagNotFound) {
        tag = model.getOrCreateTag(command, ExtensionString());
    } else {
        // menu is already there
        return NO_ERROR;
    }

    GtkWidget* menuBar = GetMenuBar(browser);
    GtkWidget* menuWidget = gtk_menu_new();
    GtkWidget* menuHeader = gtk_menu_item_new_with_label(title.c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuHeader), menuWidget);
    model.setOsItem(tag, menuHeader);
    int position = GetPosition(positionString, relativeId, menuBar, model);
    if (position >= 0)
        gtk_menu_shell_insert(GTK_MENU_SHELL(menuBar), menuHeader, position);
    else
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), menuHeader);
    gtk_widget_show(menuHeader);
    
    return NO_ERROR;
}

static void Callback(GtkMenuItem* menuItem, gpointer userData) {
    if (g_handler.get() && g_handler->GetBrowserId()) {
        int tag = GPOINTER_TO_INT(userData);
        ExtensionString commandId = NativeMenuModel::getInstance(getMenuParent(g_handler->GetBrowser())).getCommandId(tag);
        CefRefPtr<CommandCallback> callback = new EditCommandCallback(g_handler->GetBrowser(), commandId);
        g_handler->SendJSCommand(g_handler->GetBrowser(), commandId, callback);
    }
}


ExtensionString GetDisplayKeyString(const ExtensionString& keyStr)
{
    ExtensionString result = keyStr;

    // We get a keyStr that looks like "Ctrl-O", which is the format we
    // need for the accelerator table. For displaying in the menu, though,
    // we have to change it to "Ctrl+O". Careful not to change the final
    // character, though, so "Ctrl--" ends up as "Ctrl+-".
    if (result.size() > 0) {
        replace(result.begin(), result.end() - 1, '-', '+');
    }
    return result;
}


static int32 ParseShortcut(CefRefPtr<CefBrowser> browser, GtkWidget* entry, ExtensionString& key, GdkModifierType* modifier, guint* keyVal, ExtensionString& commandId) {
    if (key.size()) {
        GtkAccelGroup *accel_group = NULL;
        accel_group = gtk_accel_group_new();

        std::vector<std::string> tokens;
        std::istringstream f(key);
        std::string s;
        while (getline(f, s, '-')) {
            tokens.push_back(s);
        }
        std::string shortcut = "";
        // convert shortcut format
        // e.g. Ctrl+A converts to <Ctrl>A whose entry is stored in accelerator table
        for (int i=0;i<tokens.size();i++) {
            if (i != tokens.size() - 1) {
                shortcut += "<";
            }
            shortcut += tokens[i];
            if (i != tokens.size() - 1) {
                shortcut += ">";
            }
        }
        gchar* val = (gchar*)shortcut.c_str();
        gtk_accelerator_parse(val, keyVal, modifier);

        // Fallback
        if (!(*keyVal)) {
            for (int i=0;i<tokens.size();i++) {
                if (tokens[i] == "Ctrl") {
                    *modifier = GdkModifierType(*modifier | GDK_CONTROL_MASK);
                } else if (tokens[i] == "Alt") {
                    *modifier = GdkModifierType(*modifier | GDK_MOD1_MASK);
                } else if (tokens[i] == "Shift") {
                    *modifier = GdkModifierType(*modifier | GDK_SHIFT_MASK);
                } else if (tokens[i] == "Enter") {
                    *keyVal = GDK_KEY_KP_Enter;
                } else if (tokens[i] == "Up" || tokens[i] == "\u2191") {
                    *keyVal = GDK_KEY_uparrow;
                } else if (tokens[i] == "Down" || tokens[i] == "\u2193") {
                    *keyVal = GDK_KEY_downarrow;
                } else if (tokens[i] == "Right") {
                    *keyVal = GDK_KEY_rightarrow;
                } else if (tokens[i] == "Left") {
                    *keyVal = GDK_KEY_leftarrow;
                } else if (tokens[i] == "Space") {
                    *keyVal = GDK_KEY_KP_Space;
                } else if (tokens[i] == "PageUp") {
                    *keyVal = GDK_KEY_Page_Up;
                } else if (tokens[i] == "PageDown") {
                    *keyVal = GDK_KEY_Page_Down;
                } else if (tokens[i] == "[" || tokens[i] == "]" || tokens[i] == "+" || tokens[i] == "." || tokens[i] == "," || tokens[i] == "\\" || tokens[i] == "/" || tokens[i] == "`") {
                    *keyVal = tokens[i][0];
                } else if (tokens[i] == "−") {
                    *keyVal = GDK_KEY_minus;
                }
            }
        }
        if (*keyVal) {
            gtk_widget_add_accelerator(entry, "activate", accel_group, *keyVal, *modifier, GTK_ACCEL_VISIBLE);
        } else if (shortcut.size()) {
            // If fallback also fails, then
            // we append the shorcut to menu title
            // e.g. MENU_TITLE (Ctrl + A)
            ExtensionString menuTitle;
            GetMenuTitle(browser, commandId, menuTitle);
            menuTitle +=  "\t( " + GetDisplayKeyString(key) + " )";
            gtk_menu_item_set_label(GTK_MENU_ITEM(entry), menuTitle.c_str());
        }
    }
    return NO_ERROR;
}

int32 AddMenuItem(CefRefPtr<CefBrowser> browser, ExtensionString parentCommand, ExtensionString itemTitle,
                  ExtensionString command, ExtensionString key, ExtensionString displayStr,
                  ExtensionString positionString, ExtensionString relativeId)
{
    NativeMenuModel& model = NativeMenuModel::getInstance(getMenuParent(browser));
    int parentTag = model.getTag(parentCommand);
    if (parentTag == kTagNotFound) {
        return ERR_NOT_FOUND;
    }

    int tag = model.getTag(command);
    if (tag == kTagNotFound) {
        tag = model.getOrCreateTag(command, parentCommand);
    } else {
        return NO_ERROR;
    }

    GtkWidget* entry;
    if (itemTitle == "---")
        entry = gtk_separator_menu_item_new();
    else
        entry = gtk_menu_item_new_with_label(itemTitle.c_str());
    g_signal_connect(entry, "activate", G_CALLBACK(Callback), GINT_TO_POINTER(tag));
    GdkModifierType modifier = GdkModifierType(0);
    guint keyVal = 0;
    ExtensionString commandId = model.getCommandId(tag);
    model.setOsItem(tag, entry);
    ParseShortcut(browser, entry, key, &modifier, &keyVal, commandId);
    GtkWidget* menuHeader = (GtkWidget*) model.getOsItem(parentTag);
    GtkWidget* menuWidget = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menuHeader));
    int position = GetPosition(positionString, relativeId, menuWidget, model);
    if (position >= 0)
        gtk_menu_shell_insert(GTK_MENU_SHELL(menuWidget), entry, position);
    else
        gtk_menu_shell_append(GTK_MENU_SHELL(menuWidget), entry);
    gtk_widget_show(entry);

    return NO_ERROR;
}

int32 RemoveMenu(CefRefPtr<CefBrowser> browser, const ExtensionString& commandId)
{
    // works for menu and menu item
    return RemoveMenuItem(browser, commandId);
}

int32 RemoveMenuItem(CefRefPtr<CefBrowser> browser, const ExtensionString& commandId)
{
    NativeMenuModel& model = NativeMenuModel::getInstance(getMenuParent(browser));
    int tag = model.getTag(commandId);
    if (tag == kTagNotFound)
        return ERR_NOT_FOUND;
    GtkWidget* menuItem = (GtkWidget*) model.getOsItem(tag);
    model.removeMenuItem(commandId);
    gtk_widget_destroy(menuItem);
    return NO_ERROR;
}

int32 GetMenuItemState(CefRefPtr<CefBrowser> browser, ExtensionString commandId, bool& enabled, bool& checked, int& index)
{
    return NO_ERROR;
}

int32 SetMenuTitle(CefRefPtr<CefBrowser> browser, ExtensionString commandId, ExtensionString menuTitle)
{
    NativeMenuModel& model = NativeMenuModel::getInstance(getMenuParent(browser));
    int tag = model.getTag(commandId);
    if (tag == kTagNotFound)
        return ERR_NOT_FOUND;
    GtkWidget* menuItem = (GtkWidget*) model.getOsItem(tag);
    ExtensionString shortcut;
    GetMenuTitle(browser, commandId, shortcut);
    size_t pos = shortcut.find("\t");
    if (pos != -1) {
        shortcut = shortcut.substr(pos);
    } else {
        shortcut = "";
    }

    ExtensionString newTitle = menuTitle;
    if (shortcut.length() > 0) {
         newTitle += shortcut;
    }
    gtk_menu_item_set_label(GTK_MENU_ITEM(menuItem), newTitle.c_str());
    return NO_ERROR;
}

int32 GetMenuTitle(CefRefPtr<CefBrowser> browser, ExtensionString commandId, ExtensionString& menuTitle)
{
    NativeMenuModel& model = NativeMenuModel::getInstance(getMenuParent(browser));
    int tag = model.getTag(commandId);
    if (tag == kTagNotFound)
        return ERR_NOT_FOUND;
    GtkWidget* menuItem = (GtkWidget*) model.getOsItem(tag);
    menuTitle = gtk_menu_item_get_label(GTK_MENU_ITEM(menuItem));
    return NO_ERROR;
}

int32 SetMenuItemShortcut(CefRefPtr<CefBrowser> browser, ExtensionString commandId, ExtensionString shortcut, ExtensionString displayStr)
{
    NativeMenuModel model = NativeMenuModel::getInstance(getMenuParent(browser));
    int32 tag = model.getTag(commandId);
    if (tag == kTagNotFound) {
        return ERR_NOT_FOUND;
    }
    GtkWidget* entry = (GtkWidget*) model.getOsItem(tag);
    GdkModifierType modifier = GdkModifierType(0);
    guint keyVal = 0;
    ParseShortcut(browser, entry, shortcut, &modifier, &keyVal, commandId);

    return NO_ERROR;
}

int32 GetMenuPosition(CefRefPtr<CefBrowser> browser, const ExtensionString& commandId, ExtensionString& parentId, int& index)
{
    return NO_ERROR;
}

void DragWindow(CefRefPtr<CefBrowser> browser)
{
    // TODO
}

