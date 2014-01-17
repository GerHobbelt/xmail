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

#ifndef _USRUTILS_H
#define _USRUTILS_H

#define INVALID_USRF_HANDLE         ((USRF_HANDLE) 0)
#define INVALID_ALSF_HANDLE         ((ALSF_HANDLE) 0)

#define GMPROC_USER (1 << 0)
#define GMPROC_DOMAIN (1 << 1)

struct UserInfo {
	char *pszDomain;
	unsigned int uUserID;
	char *pszName;
	char *pszPassword;
	char *pszPath;
	char *pszType;
	HSLIST InfoList;
};

struct AliasInfo {
	char *pszDomain;
	char *pszAlias;
	char *pszName;
};

enum UserType {
	usrTypeError = -1,
	usrTypeUser = 0,
	usrTypeML,

	usrTypeMax
};

typedef struct USRF_HANDLE_struct {
} *USRF_HANDLE;

typedef struct ALSF_HANDLE_struct {
} *ALSF_HANDLE;

int UsrCheckUsersIndexes(void);
int UsrCheckAliasesIndexes(void);
char *UsrGetMLTableFilePath(UserInfo *pUI, char *pszMLTablePath, int iMaxPath);
UserType UsrGetUserType(UserInfo *pUI);
UserInfo *UsrCreateDefaultUser(char const *pszDomain, char const *pszName,
			       char const *pszPassword, UserType TypeUser);
void UsrFreeUserInfo(UserInfo *pUI);
char *UsrGetUserInfoVar(UserInfo *pUI, char const *pszName, char const *pszDefault = NULL);
int UsrGetUserInfoVarInt(UserInfo *pUI, char const *pszName, int iDefault);
int UsrDelUserInfoVar(UserInfo *pUI, char const *pszName);
int UsrSetUserInfoVar(UserInfo *pUI, char const *pszName, char const *pszValue);
char **UsrGetProfileVars(UserInfo *pUI);
int UsrAliasLookupName(char const *pszDomain, char const *pszAlias,
		       char *pszName = NULL, bool bWildMatch = true);
AliasInfo *UsrAllocAlias(char const *pszDomain, char const *pszAlias, char const *pszName);
void UsrFreeAlias(AliasInfo *pAI);
int UsrAddAlias(AliasInfo *pAI);
int UsrRemoveAlias(char const *pszDomain, char const *pszAlias);
int UsrRemoveDomainAliases(char const *pszDomain);
UserInfo *UsrLookupUser(char const *pszDomain, char const *pszName);
UserInfo *UsrGetUserByName(char const *pszDomain, char const *pszName);
UserInfo *UsrGetUserByNameOrAlias(char const *pszDomain, char const *pszName,
				  char *pszRealAddr = NULL);
int UsrRemoveUser(char const *pszDomain, char const *pszName, unsigned int uUserID);
int UsrModifyUser(UserInfo *pUI);
int UsrRemoveDomainUsers(char const *pszDomain);
int UsrAddUser(UserInfo *pUI);
int UsrFlushUserVars(UserInfo *pUI);
int UsrGetDBFileSnapShot(char const *pszFileName);
USRF_HANDLE UsrOpenDB(void);
void UsrCloseDB(USRF_HANDLE hUsersDB);
UserInfo *UsrGetFirstUser(USRF_HANDLE hUsersDB, int iLoadUCfg);
UserInfo *UsrGetNextUser(USRF_HANDLE hUsersDB, int iLoadUCfg);
int UsrPOP3Lock(UserInfo *pUI);
void UsrPOP3Unlock(UserInfo *pUI);
int UsrClearPop3LocksDir(void);
int UsrGetTmpFile(char const *pszDomain, char *pszTmpFile, int iMaxPath);
char *UsrGetUserPath(UserInfo *pUI, char *pszUserPath, int iMaxPath, int iFinalSlash);
char *UsrGetMailboxPath(UserInfo *pUI, char *pszMBPath, int iMaxPath, int iFinalSlash);
int UsrMoveToMailBox(UserInfo *pUI, char const *pszFileName, char const *pszMessageID);
int UsrGetMailProcessFile(UserInfo *pUI, char const *pszMPPath, unsigned long ulFlags);
int UsrSetMailProcessFile(UserInfo *pUI, char const *pszMPPath, int iWhich);
char *UsrGetAddress(UserInfo *pUI, char *pszAddress);
int UsrGetAliasDBFileSnapShot(char const *pszFileName);
ALSF_HANDLE UsrAliasOpenDB(void);
void UsrAliasCloseDB(ALSF_HANDLE hAliasDB);
AliasInfo *UsrAliasGetFirst(ALSF_HANDLE hAliasDB);
AliasInfo *UsrAliasGetNext(ALSF_HANDLE hAliasDB);

#endif
