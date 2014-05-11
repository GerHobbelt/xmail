/*
 *  XMail by Davide Libenzi (Intranet and Internet mail server)
 *  Copyright (C) 1999,..,2010  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "SList.h"
#include "ShBlocks.h"
#include "BuffSock.h"
#include "SSLBind.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MiscUtils.h"
#include "SSLConfig.h"
#include "MailConfig.h"
#include "AppDefines.h"


int CSslBindSetup(SslServerBind *pSSLB)
{
    SVRCFG_HANDLE hCfg;
    char szMailRoot[SYS_MAX_PATH] = "", szPath[SYS_MAX_PATH] = "";

    if ((hCfg = SvrGetConfigHandle()) == INVALID_SVRCFG_HANDLE)
        return ErrGetErrorCode();

    ZeroData(*pSSLB);
    CfgGetRootPath(szMailRoot, sizeof(szMailRoot));

    SysSNPrintf(szPath, sizeof(szPath) - 1, "%sserver.cert", szMailRoot);
    pSSLB->pszCertFile = SysStrDup(szPath);

    SysSNPrintf(szPath, sizeof(szPath) - 1, "%sserver.key", szMailRoot);
    pSSLB->pszKeyFile = SysStrDup(szPath);

    if (SvrTestConfigFlag("SSLUseCertsFile", false, hCfg)) {
        SysSNPrintf(szPath, sizeof(szPath) - 1, "%scerts.pem", szMailRoot);
        if (SysExistFile(szPath))
            pSSLB->pszCAFile = SysStrDup(szPath);
    }

    if (SvrTestConfigFlag("SSLUseCertsDir", false, hCfg)) {
        SysSNPrintf(szPath, sizeof(szPath) - 1, "%scerts", szMailRoot);
        if (SysExistDir(szPath))
            pSSLB->pszCAPath = SysStrDup(szPath);
    }

    if (SvrTestConfigFlag("SSLWantVerify", false, hCfg))
        pSSLB->ulFlags |= BSSLF_WANT_VERIFY;

    if (SvrTestConfigFlag("SSLAllowSelfSigned", false, hCfg))
        pSSLB->ulFlags |= BSSLF_ALLOW_SEFLSIGNED;

    if (SvrTestConfigFlag("SSLWantCert", false, hCfg))
        pSSLB->ulFlags |= BSSLF_WANT_VERIFY | BSSLF_WANT_CERT;

    pSSLB->iMaxDepth = SvrGetConfigInt("SSLMaxCertsDepth", 0, hCfg);

    SvrReleaseConfigHandle(hCfg);


    return 0;
}

void CSslBindCleanup(SslServerBind *pSSLB)
{

    SysFree(pSSLB->pszKeyFile);
    SysFree(pSSLB->pszCertFile);
    SysFree(pSSLB->pszCAFile);
    SysFree(pSSLB->pszCAPath);
}

