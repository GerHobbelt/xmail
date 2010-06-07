# Microsoft Developer Studio Project File - Name="mailsvr" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=mailsvr - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mailsvr.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mailsvr.mak" CFG="mailsvr - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mailsvr - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "mailsvr - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "mailsvr - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "XMAIL_OS=Win32" /YX /FD /c
# ADD CPP /nologo /Zp1 /MT /W3 /GX /O2 /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "XMAIL_OS=Win32" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /subsystem:console /machine:I386 /out:"Release/XMail.exe"

!ELSEIF  "$(CFG)" == "mailsvr - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "XMAIL_OS=Win32" /YX /FD /GZ /c
# ADD CPP /nologo /Zp1 /MTd /W3 /Gm /GX /ZI /Od /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "XMAIL_OS=Win32" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /subsystem:console /debug /machine:I386 /out:"Debug/XMail.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "mailsvr - Win32 Release"
# Name "mailsvr - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\AliasDomain.cpp
# End Source File
# Begin Source File

SOURCE=.\Base64Enc.cpp
# End Source File
# Begin Source File

SOURCE=.\BuffSock.cpp
# End Source File
# Begin Source File

SOURCE=.\CTRLSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\DNS.cpp
# End Source File
# Begin Source File

SOURCE=.\DNSCache.cpp
# End Source File
# Begin Source File

SOURCE=.\DynDNS.cpp
# End Source File
# Begin Source File

SOURCE=.\Errors.cpp
# End Source File
# Begin Source File

SOURCE=.\ExtAliases.cpp
# End Source File
# Begin Source File

SOURCE=.\Filter.cpp
# End Source File
# Begin Source File

SOURCE=.\FINGSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\LMAILSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\MailConfig.cpp
# End Source File
# Begin Source File

SOURCE=.\Maildir.cpp
# End Source File
# Begin Source File

SOURCE=.\MailDomains.cpp
# End Source File
# Begin Source File

SOURCE=.\MailSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\Main.cpp
# End Source File
# Begin Source File

SOURCE=.\MainLinux.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\MainSolaris.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\MainWin.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\MD5.cpp
# End Source File
# Begin Source File

SOURCE=.\MessQueue.cpp
# End Source File
# Begin Source File

SOURCE=.\MiscUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\POP3GwLink.cpp
# End Source File
# Begin Source File

SOURCE=.\POP3Svr.cpp
# End Source File
# Begin Source File

SOURCE=.\POP3Utils.cpp
# End Source File
# Begin Source File

SOURCE=.\PSYNCSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\QueueUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\ResLocks.cpp
# End Source File
# Begin Source File

SOURCE=.\ShBlocks.cpp
# End Source File
# Begin Source File

SOURCE=.\SList.cpp
# End Source File
# Begin Source File

SOURCE=.\SMAILSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\SMAILUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\SMTPSvr.cpp
# End Source File
# Begin Source File

SOURCE=.\SMTPUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\StrUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\SvrUtils.cpp
# End Source File
# Begin Source File

SOURCE=.\SysDep.cpp
# End Source File
# Begin Source File

SOURCE=.\SysDepLinux.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\SysDepSolaris.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\SysDepWin.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\TabIndex.cpp
# End Source File
# Begin Source File

SOURCE=.\UsrAuth.cpp
# End Source File
# Begin Source File

SOURCE=.\UsrMailList.cpp
# End Source File
# Begin Source File

SOURCE=.\UsrUtils.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\AliasDomain.h
# End Source File
# Begin Source File

SOURCE=.\AppDefines.h
# End Source File
# Begin Source File

SOURCE=.\Base64Enc.h
# End Source File
# Begin Source File

SOURCE=.\BuffSock.h
# End Source File
# Begin Source File

SOURCE=.\CTRLSvr.h
# End Source File
# Begin Source File

SOURCE=.\DNS.h
# End Source File
# Begin Source File

SOURCE=.\DNSCache.h
# End Source File
# Begin Source File

SOURCE=.\DynDNS.h
# End Source File
# Begin Source File

SOURCE=.\Errors.h
# End Source File
# Begin Source File

SOURCE=.\ExtAliases.h
# End Source File
# Begin Source File

SOURCE=.\Filter.h
# End Source File
# Begin Source File

SOURCE=.\FINGSvr.h
# End Source File
# Begin Source File

SOURCE=.\LMAILSvr.h
# End Source File
# Begin Source File

SOURCE=.\MailConfig.h
# End Source File
# Begin Source File

SOURCE=.\Maildir.h
# End Source File
# Begin Source File

SOURCE=.\MailDomains.h
# End Source File
# Begin Source File

SOURCE=.\MailSvr.h
# End Source File
# Begin Source File

SOURCE=.\MD5.h
# End Source File
# Begin Source File

SOURCE=.\MessQueue.h
# End Source File
# Begin Source File

SOURCE=.\MiscUtils.h
# End Source File
# Begin Source File

SOURCE=.\POP3GwLink.h
# End Source File
# Begin Source File

SOURCE=.\POP3Svr.h
# End Source File
# Begin Source File

SOURCE=.\POP3Utils.h
# End Source File
# Begin Source File

SOURCE=.\PSYNCSvr.h
# End Source File
# Begin Source File

SOURCE=.\QueueUtils.h
# End Source File
# Begin Source File

SOURCE=.\ResLocks.h
# End Source File
# Begin Source File

SOURCE=.\ShBlocks.h
# End Source File
# Begin Source File

SOURCE=.\SList.h
# End Source File
# Begin Source File

SOURCE=.\SMAILSvr.h
# End Source File
# Begin Source File

SOURCE=.\SMAILUtils.h
# End Source File
# Begin Source File

SOURCE=.\SMTPSvr.h
# End Source File
# Begin Source File

SOURCE=.\SMTPUtils.h
# End Source File
# Begin Source File

SOURCE=.\StrUtils.h
# End Source File
# Begin Source File

SOURCE=.\SvrConfig.h
# End Source File
# Begin Source File

SOURCE=.\SvrDefines.h
# End Source File
# Begin Source File

SOURCE=.\SvrUtils.h
# End Source File
# Begin Source File

SOURCE=.\SysDep.h
# End Source File
# Begin Source File

SOURCE=.\SysInclude.h
# End Source File
# Begin Source File

SOURCE=.\SysIncludeLinux.h
# End Source File
# Begin Source File

SOURCE=.\SysIncludeSolaris.h
# End Source File
# Begin Source File

SOURCE=.\SysIncludeWin.h
# End Source File
# Begin Source File

SOURCE=.\SysLists.h
# End Source File
# Begin Source File

SOURCE=.\SysMacros.h
# End Source File
# Begin Source File

SOURCE=.\SysTypes.h
# End Source File
# Begin Source File

SOURCE=.\SysTypesLinux.h
# End Source File
# Begin Source File

SOURCE=.\SysTypesSolaris.h
# End Source File
# Begin Source File

SOURCE=.\SysTypesWin.h
# End Source File
# Begin Source File

SOURCE=.\TabIndex.h
# End Source File
# Begin Source File

SOURCE=.\UsrAuth.h
# End Source File
# Begin Source File

SOURCE=.\UsrMailList.h
# End Source File
# Begin Source File

SOURCE=.\UsrUtils.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
