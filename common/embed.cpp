/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 20007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pelle Johnsen <pjohnsen@mozilla.com>
 *   Dave Camp <dcamp@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "xpcom-config.h"
#include "mozilla-config.h"

#include "embed.h"

// CRT headers
#include <iostream>
#include <string>
using namespace std;

#ifdef WIN32
  //TODO: make this file fully X platform
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define MAX_PATH _MAX_PATH
#else
#  define MAX_PATH PATH_MAX
#endif


// Mozilla Frozen APIs
#include "nsXULAppAPI.h"
#include "nsXPCOMGlue.h"
#include "nsCOMPtr.h"
#include "nsStringAPI.h"
#include "nsILocalFile.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsProfileDirServiceProvider.h"

#include "nsIWebBrowser.h"
#include "nsIWebNavigation.h"
#include "nsEmbedCID.h"

#include "nsIWebBrowserFocus.h"

// Non-Frozen
#include "nsIBaseWindow.h"

// our stuff
#include "WebBrowserChrome.h"

XRE_InitEmbeddingType XRE_InitEmbedding = NULL;
XRE_TermEmbeddingType XRE_TermEmbedding = NULL;

#ifdef MOZ_WIDGET_GTK2
#include "nsIDirectoryService.h"
#include "nsAppDirectoryServiceDefs.h"
nsIDirectoryServiceProvider *sAppFileLocProvider = nsnull;
nsCOMPtr<nsILocalFile> sProfileDir = nsnull;

class GTKEmbedDirectoryProvider : public nsIDirectoryServiceProvider2
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDIRECTORYSERVICEPROVIDER
  NS_DECL_NSIDIRECTORYSERVICEPROVIDER2
};

static const GTKEmbedDirectoryProvider kDirectoryProvider;

NS_IMPL_QUERY_INTERFACE2(GTKEmbedDirectoryProvider,
                         nsIDirectoryServiceProvider,
                         nsIDirectoryServiceProvider2)

NS_IMETHODIMP_(nsrefcnt)
GTKEmbedDirectoryProvider::AddRef()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
GTKEmbedDirectoryProvider::Release()
{
  return 1;
}

NS_IMETHODIMP
GTKEmbedDirectoryProvider::GetFile(const char *aKey, PRBool *aPersist,
                                   nsIFile* *aResult)
{
  if (sAppFileLocProvider) {
    nsresult rv = sAppFileLocProvider->GetFile(aKey, aPersist,
                                                             aResult);
    if (NS_SUCCEEDED(rv))
      return rv;
  }

  if (sProfileDir && !strcmp(aKey, NS_APP_USER_PROFILE_50_DIR)) {
    *aPersist = PR_TRUE;
    return sProfileDir->Clone(aResult);
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
GTKEmbedDirectoryProvider::GetFiles(const char *aKey,
                                    nsISimpleEnumerator* *aResult)
{
  nsCOMPtr<nsIDirectoryServiceProvider2>
    dp2(do_QueryInterface(sAppFileLocProvider));

  if (!dp2)
    return NS_ERROR_FAILURE;

  return dp2->GetFiles(aKey, aResult);
}
#endif

nsresult StartupProfile()
{
    nsCOMPtr<nsIFile> appDataDir;
    nsresult rv = NS_GetSpecialDirectory(NS_APP_APPLICATION_REGISTRY_DIR, getter_AddRefs(appDataDir));
    if (NS_FAILED(rv))
        return rv;

    appDataDir->AppendNative(nsCString("embedTest"));
    nsCOMPtr<nsILocalFile> localAppDataDir(do_QueryInterface(appDataDir));

#ifdef MOZ_WIDGET_GTK2
    sProfileDir = localAppDataDir;
    return NS_OK;
#else
    nsCOMPtr<nsProfileDirServiceProvider> locProvider;
    NS_NewProfileDirServiceProvider(PR_TRUE, getter_AddRefs(locProvider));
    if (!locProvider)
        return NS_ERROR_FAILURE;

    rv = locProvider->Register();
    if (NS_FAILED(rv))
        return rv;

    return locProvider->SetProfileDir(localAppDataDir);
#endif
}

nsresult MozEmbed::InitEmbedding()
{
    nsresult rv;

    // Find the GRE (xul shared lib). We are only using frozen interfaces, so we
    // should be compatible all the way up to (but not including) mozilla 2.0
    static const GREVersionRange vr = {
        "1.9a1",
        PR_TRUE,
        "2.0",
        PR_FALSE
    };
    // find xpcom shared lib (uses GRE_HOME env var if set)
    char temp[MAX_PATH];
    rv = GRE_GetGREPathWithProperties(&vr, 1, nsnull, 0,
        temp, sizeof(temp));
    string xpcomPath(temp);
    cout << "xpcom: " << xpcomPath << endl;

    if (NS_FAILED(rv)) {
        cerr << "Unable to find GRE, try setting GRE_HOME" << endl;
        return 1;
    }

    // start the glue, i.e. load and link against xpcom shared lib
    rv = XPCOMGlueStartup(xpcomPath.c_str());
    if (NS_FAILED(rv)) {
        cerr << "Couldn't start XPCOM glue" << endl;
        return 2;
    }

    // load XUL functions
    nsDynamicFunctionLoad nsFuncs[] = {
            {"XRE_InitEmbedding", (NSFuncPtr*)&XRE_InitEmbedding},
            {"XRE_TermEmbedding", (NSFuncPtr*)&XRE_TermEmbedding},
            {0, 0}
    };

    cout << "here?";

    rv = XPCOMGlueLoadXULFunctions(nsFuncs);
    if (NS_FAILED(rv)) {
        cerr << "Couldn't load XUL functions" << endl;
        return 4;
    }

    // strip the filename from xpcom so we have the dir instead
    size_t lastslash = xpcomPath.find_last_of("/\\");
    if (lastslash == string::npos) {
        cerr << "Invalid path to xpcom: %s" << endl;
        return 3;
    }
    string xpcomDir = xpcomPath.substr(0, lastslash);

    // create nsILocalFile pointing to xpcomDir
    nsCOMPtr<nsILocalFile> xuldir;
    rv = NS_NewNativeLocalFile(nsCString(xpcomDir.c_str()), PR_FALSE,
                               getter_AddRefs(xuldir));
    if (NS_FAILED(rv)) {
      cerr << "Unable to create nsILocalFile for xuldir " << xpcomDir << endl;
        return 6;
    }

#ifdef WIN32
    // create nsILocalFile pointing to appdir (WIN32)
    char self[MAX_PATH];
    GetModuleFileNameA(GetModuleHandle(NULL), self, sizeof(self));
    string selfPath(self);
    lastslash = selfPath.find_last_of("/\\");
    if (lastslash == string::npos) {
        cerr << "Invalid module filename: " << self << endl;
        return 7;
    }

    selfPath = selfPath.substr(0, lastslash);
#else
    // XXX: crap!
    string selfPath = "/home/dave/moz/embed/linux";
#endif

    nsCOMPtr<nsILocalFile> binDir;
    nsCOMPtr<nsILocalFile> appdir;
    rv = NS_NewNativeLocalFile(nsCString(selfPath.c_str()), PR_FALSE,
                               getter_AddRefs(appdir));
    if (NS_FAILED(rv)) {
        cerr << "Unable to create nsILocalFile for appdir" << endl;
  8;
    }

    printf("b\n");
    // init embedding
    rv = XRE_InitEmbedding(xuldir, appdir, const_cast<GTKEmbedDirectoryProvider*>(&kDirectoryProvider), nsnull, 0);
    if (NS_FAILED(rv)) {
        cerr << "XRE_InitEmbedding failed" << endl;
        return 9;
    }

    // profile
    rv = StartupProfile();
    if (NS_FAILED(rv)) {
        return 10;
    }

    return NS_OK;
}

nsresult MozEmbed::CreateBrowser(void* aNativeWindow, PRInt32 x, PRInt32 y, PRInt32 width, PRInt32 height)
{
    nativeWindow = aNativeWindow;

    nsresult rv;

    nsCOMPtr<nsIBaseWindow> baseWindow;
    webBrowser = do_CreateInstance(NS_WEBBROWSER_CONTRACTID, &rv);
    if (NS_FAILED(rv)) {
            printf("do_CreateInstance webBrowser\n");
    }
    baseWindow = do_QueryInterface(webBrowser);
    rv = baseWindow->InitWindow(nativeWindow, 0, x, y, width, height);
    if (NS_FAILED(rv)) {
            printf("InitWindow\n");
    }
    
    nsIWebBrowserChrome **aNewWindow = getter_AddRefs(chrome);
    CallQueryInterface(static_cast<nsIWebBrowserChrome*>(new WebBrowserChrome(this)), aNewWindow);
    webBrowser->SetContainerWindow(chrome.get());
    chrome->SetWebBrowser(webBrowser);

    rv = baseWindow->Create();
    if (NS_FAILED(rv)) {
            printf("Create\n");
    }
    rv =baseWindow->SetVisibility(PR_TRUE);
    if (NS_FAILED(rv)) {
            printf("SetVisibility\n");
    }

    webNavigation = do_QueryInterface(webBrowser);
    
    SetFocus(true);

    return 0;
}

MozEmbed::MozEmbed()
: nativeWindow(NULL), pListener(NULL)
{
    InitEmbedding();
}

MozEmbed::~MozEmbed()
{
    nsresult rv;

    // release browser and chrome
    nsCOMPtr<nsIBaseWindow> baseWindow;
    baseWindow = do_QueryInterface(webBrowser);
    baseWindow->Destroy();
    chrome->SetWebBrowser(NULL);
    
    baseWindow = NULL;
    webBrowser = NULL;
    chrome = NULL;

    // terminate embedding
    if (!XRE_TermEmbedding) {
        cerr << "XRE_TermEmbedding not set" << endl;
        return;
    }
    XRE_TermEmbedding();

    // shutdown xpcom
    rv = XPCOMGlueShutdown();
    if (NS_FAILED(rv)) {
        fprintf(stderr, "Couldn't shutdown XPCOM glue\n");
        return;
    }    
}

nsresult MozEmbed::SetPositionAndSize(PRInt32 x, PRInt32 y, PRInt32 width, PRInt32 height)
{
    nsresult rv;
    nsCOMPtr<nsIBaseWindow> baseWindow;
    baseWindow = do_QueryInterface(webBrowser);
    rv = baseWindow->SetPositionAndSize(x, y, width, height, PR_TRUE);
    if (NS_FAILED(rv))
        return 1;
    else
        return 0;
}

nsresult MozEmbed::LoadURI(const char* uri)
{
    nsresult rv;
    rv = webNavigation->LoadURI(NS_ConvertASCIItoUTF16(uri).get(),
        nsIWebNavigation::LOAD_FLAGS_NONE, 0, 0, 0);
    return rv;
}

nsresult MozEmbed::SetFocus(PRBool focus)
{
    nsCOMPtr<nsIWebBrowserFocus> browserFocus;
    browserFocus = do_QueryInterface(webBrowser);
    if(focus)
        browserFocus->Activate();
    else
        browserFocus->Deactivate();
    return NS_OK;
}

void MozEmbed::SetListener(EmbedListener *pNewListener)
{
    pListener = pNewListener;
    pListener->SetMozEmbed(this);
}

EmbedListener* MozEmbed::GetListener()
{
    return pListener;
}

void* MozEmbed::GetNativeWindow()
{
    return nativeWindow;
}

// ---- EmbedListener ---
EmbedListener::EmbedListener()
: pMozEmbed(NULL)
{
}

EmbedListener::~EmbedListener()
{
}

void EmbedListener::SetTitle(const char *newTitle)
{
}

void EmbedListener::SetMozEmbed(MozEmbed *pAMozEmbed)
{
    pMozEmbed = pAMozEmbed;
}
