/*---------------------------------------------------------------------------
 *
 *  Projeto WxWeb
 *
 *  Inicio...: Maio / 2006
 *
 *  Revisado.: 24/11/2009 - 12:45:55
 *
 *  Por......: Vailton Renato da Silva
 *
 *  Arquivo..: wxAutoLoad.prg
 *
 *  Fun��es para carregamento automatico da wxWeb em ambiente CGI
 *
 *---------------------------------------------------------------------------*/

ANNOUNCE wxAutoLoad

REQUEST WXWEB
REQUEST WXCONNECTION_PREPARE
REQUEST WXCONNECTION_INIT
REQUEST WXCONNECTION_EXIT
REQUEST WXCONNECTION_CACHETYPE
REQUEST WXCONNECTION_CREATE
REQUEST WXCONNECTION_DESTROY
REQUEST WXQQOUT
REQUEST WXQOUT
REQUEST WXQQOUTDIRECT
REQUEST WXQOUTDIRECT
REQUEST WXSENDHEADERDIRECT
REQUEST WXSENDHEADER
REQUEST WXWEBREGISTEROUTPUTFUNC
REQUEST WXSERVERNAME
REQUEST WXSERVER
REQUEST WXSERVERCOUNT
REQUEST WXGETENV
REQUEST WXGETFIELDCOUNT
REQUEST WXGETFIELD
REQUEST WXFIELDEXISTS
REQUEST WXGETFIELDNAME
REQUEST WXGETCOOKIECOUNT
REQUEST WXGETCOOKIE
REQUEST WXCOOKIEEXISTS
REQUEST WXGETCOOKIENAME
REQUEST WXGETCONFIG
REQUEST WXLOADCONFIGFROMFILE
REQUEST WXREDIRECT
REQUEST WXGETDEFAULTPARAM

REQUEST  SESSION
REQUEST SESSION_CLEAR
REQUEST SESSION_COUNT
REQUEST SESSION_EXIST
REQUEST SESSION_GETNAME
REQUEST SESSION_ID
REQUEST SESSION_NAME
REQUEST SESSION_SAVEPATH
REQUEST SESSION_WRITE
REQUEST SESSION_STARTED
REQUEST SESSION_START
REQUEST  SESSION_SET_COOKIE_PARAMS
REQUEST  SESSION_DESTROY
REQUEST  WXSERIALIZE
REQUEST  WXITEMDESERIALIZE
REQUEST  PRINTF
REQUEST  PRINTF
REQUEST  SPRINTF
REQUEST  WXBASE64_ENCODE
REQUEST  WXBASE64_DECODE
REQUEST MD5
REQUEST MD5_FILE

REQUEST  WXWEBVERSION
REQUEST  WXWEBVERSIONSTR
REQUEST  WXGETAPPMODE
REQUEST  WXSETAPPMODE
REQUEST  WXWEB
REQUEST  WXEXENAME
REQUEST  WXEXEPATH
REQUEST  WXBUILDDATE
REQUEST  WXBUILDTIME
REQUEST  XWEBLOADPRGSOURCE
REQUEST  WXERRORGETTEMPLATE


INIT PROCEDURE wxAutoLoadInit()
   wxWebCGI( HB_ThreadID() )
   RETURN

EXIT PROCEDURE wxAutoLoadExit()
   wxConnection_Exit()
   RETURN
