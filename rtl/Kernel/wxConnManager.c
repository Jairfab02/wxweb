/*---------------------------------------------------------------------------
 *
 *  Projeto WxWeb
 *
 *  Inicio...: Maio / 2006
 *
 *  Revisado C ..: 02/07/2008 - 22:27:01
 *
 *  Revisado C++.: 08/12/2006 - 08:09:11
 *
 *  Por......: Vailton Renato da Silva
 *
 *  Arquivo..: wxConnManager.cpp
 *                            
 *  Fun��es para manipula��o da conex�o com o cliente
 *
 *---------------------------------------------------------------------------*/

#include <wxweb.h>
#include "hbvmopt.h"
#include "hbapi.h"
#include "hbapiitm.h"
#include "hbapifs.h"
#include "hbapierr.h"
#include "hbstack.h"
#include "hbmath.h"
#include "hbvm.h"

#ifdef HB_OS_WIN
   #include <io.h>
#endif

#ifndef VERSAO_COMERCIAL
   #include <time.h>
#endif
#include <string.h>
#include <wxMemory.h>
#include <wxTrace.h>
#include "wxConnManager.h"
#include <wxSession.h>

static PConnection pConnList = NULL;
static BOOL sbStarted = FALSE;
static char szModuleInit[WXWEB_MAX_MODULE_LEN] = { 0 };
static char szModuleExit[WXWEB_MAX_MODULE_LEN] = { 0 };

extern char *wxUrlEncode( char *s, ULONG len, ULONG *out_size);
extern char *wxUrlDecode( char *Text, ULONG Length, ULONG *out_size );
extern char *wxAdJustPath( char *Path, char *Default );

#if defined( HB_THREAD_SUPPORT )
   static HB_CRITICAL_T pConnMutex;

   #if defined( WEB_DEBUG )   
      static char pMutexLockedModule[255];
   #else
      #define pMutexLockedModule  (NULL)
   #endif
#else
   #define pConnMutex  (NULL)
#endif

/*
 * Some macros to support documentation tool..
 * @define CONN_FUNC_LIST wxConnection_Init() wxConnection_Exit() wxConnection_Create()
 * @define CONN_FUNC_LIST wxConnection_Destroy() wxConnection_CacheType()
 *
 * @define CONIO_FUNC_LIST wxQout() WXQQOUT() WXQOUTDIRECT() WXQQOUTDIRECT()
 * @define CONIO_FUNC_LIST PRINTF() SPRINTF() SPRINTF
 */
/*
 * Obtem o nome do modulo, fun��o e linha do PRG onde estamos atualmente 
 * 22/07/2008 - 08:31:53
 */
char *wxGetModuleName( int format_type )
{
   char *Text;
   char szName[HB_SYMBOL_NAME_LEN + HB_SYMBOL_NAME_LEN + 5];
   char szModuleName[HB_SYMBOL_NAME_LEN + HB_SYMBOL_NAME_LEN + 5];
   USHORT uLine;
   int L;
         
   szModuleName[0] = '\0';
   szName[0]       = '\0';
                       
   hb_procinfo( hb_parni( 1 ) + 1, szName, &uLine, szModuleName );         

   L = strlen( szName ) + strlen( szModuleName ) + ((format_type == WX_PRG_LONG) ? 25 : 10 );
   Text = xStrNew( L );

   switch (format_type)
   {
      case WX_PRG_SHORT :   
         if (szModuleName[0])
            sprintf( Text, "%s:%s(%d)", szModuleName, szName, (int) uLine );
         else
            sprintf( Text, "%s(%d)", szName, (int) uLine );
         break;

      case WX_PRG_LONG  :   
         if (szModuleName[0])
            sprintf( Text, "%s(%d) in Module %s", szName, (int) uLine, szModuleName );
         else
            sprintf( Text, "%s(%d)", szName, (int) uLine );
         break;
   }        

   //HB_TRACE( HB_TR_DEBUG, ("wxGetModuleName( %d )  --> %s, %d", format_type, Text, L ));
   return Text;   
}

#if defined( WEB_DEBUG )
static
void ConnList_Debug( void )
{
   int i = 0;
   PConnection pClientConn;
   HB_TRACE( HB_TR_DEBUG, ("ConnList_Debug( %p )      --> ENTER", pConnList ));
   
   pClientConn = pConnList;  

   do { 
      
      i++;                       
      HB_TRACE( HB_TR_DEBUG, ("  %d - pClientConn (%d)       -> (%p) *", i, ((pClientConn) ? pClientConn->Handle : 0 ), pClientConn ));
      
      if (!pClientConn)
         break;      
      
      HB_TRACE( HB_TR_DEBUG, ("  %d - pClientConn->pNext     -> (%p) *", i, pClientConn->pNext ));
      
      pClientConn = pClientConn->pNext;
   } while( pClientConn );
   
   HB_TRACE( HB_TR_DEBUG, ("ConnList_Debug EXIT       --> EXIT", pConnList ));
}
#endif

static void wxConnection_InternalError( const int nError )
{
   char *szName;         
   char msg[255];   
	
	szName = wxGetModuleName( WX_PRG_SHORT );
 printf( ">>>%s\n", szName );
   msg[0] = '\0';
	
   switch (nError)
   {
      case WX_ERROR_SUBSYSTEM_INVALID:
      
         if (szModuleExit[0]) 
            sprintf( msg, "%s: Internal error! wxConnection_Exit() previously called in %s - memory subsystem unavailable!", szName, szModuleExit );
         else if (szModuleInit[0]) 
            sprintf( msg, "%s: Internal error! Memory subsystem unavailable!", szName );
         else  
            sprintf( msg, "%s: Internal error! Memory subsystem not initialized!", szName );
         break;
         
      default:
         sprintf( msg, "%s: Internal error!", szName );
         break;      
   }
   
   hb_xfree( szName );
   hb_errInternal( 900, msg, NULL, NULL );
   return;
}

#if defined( HB_THREAD_SUPPORT )
static void ConnList_EnterCriticalSection( char *ModuleName, WX_PTR Data)
{
   HB_TRACE( HB_TR_DEBUG, ("LOCK CRITICAL SECTION - %s(%p) || Previous [%s]", ModuleName, Data, pMutexLockedModule ));

   if (!sbStarted) 
      wxConnection_InternalError( WX_ERROR_SUBSYSTEM_INVALID );
   
   HB_CRITICAL_LOCK( pConnMutex );
   
   #if defined( WEB_DEBUG )
      if (ModuleName) sprintf( pMutexLockedModule, "%s(%p)", ModuleName, Data );
      HB_TRACE( HB_TR_DEBUG, ("     LOCKED CRITICAL SECTION --> %s -> LOCK OK!!!", pMutexLockedModule ));           
      ConnList_Debug();
   #else
      HB_SYMBOL_UNUSED( ModuleName );
      HB_SYMBOL_UNUSED( Data );      
   #endif   
   return;
}
#else
   #define ConnList_EnterCriticalSection(a,b);
#endif

#if defined( HB_THREAD_SUPPORT )
static void ConnList_LeaveCriticalSection(WX_PTR Data)
{
   HB_TRACE( HB_TR_DEBUG, ("UNLOCK CRITICAL SECTION --> [%s]", pMutexLockedModule ));

   #if defined( WEB_DEBUG )
      ConnList_Debug();
   #endif
   
   HB_CRITICAL_UNLOCK( pConnMutex );
   HB_TRACE( HB_TR_DEBUG, ("     CRITICAL SECTION - Unlocked successfully --> %p -> OK!", Data ));      

   #if defined( WEB_DEBUG )
      pMutexLockedModule[0] = '\0';
   #endif
   return;
}
#else
   #define ConnList_LeaveCriticalSection(a);
#endif

/*
 * Adiciona uma nova conex�o � listagem atual                      
 * 03/07/2008 - 08:06:42
 */
WX_RES ConnList_Register( PConnection pConn )
{
   PConnection pClientConn;
   HB_TRACE( HB_TR_DEBUG, ("ConnList_Register(%p)", pConn ));
  
   if (!sbStarted)
      return WX_FAILURE ;
      
   ConnList_EnterCriticalSection("ConnList_Register", (WX_PTR) pConn);
      
   // � a primeira conex�o?
   if (!pConnList)
   {
      HB_TRACE( HB_TR_DEBUG, ("  � a nossa primeira conex�o!!!! ---> (%p / %p)", pConnList, pConn ));
      pConn->pNext = NULL;
      pConnList    = pConn;
      
      ConnList_LeaveCriticalSection((WX_PTR) pConn);
      return WX_SUCCESS ;
   } 
   
   // H� mais de uma conex�o ativa? Pegue a �ltima!
   pClientConn = pConnList;
   
   while (pClientConn->pNext)
          pClientConn = pClientConn->pNext;  

   HB_TRACE( HB_TR_DEBUG, ("  pConnList -> Atail -> (%p  // %p)", pClientConn,pConn ));
   
   // Colocamos ele como ultimo na lista!
   if (pClientConn)
      pClientConn->pNext = pConn;          
   
   pConn->pNext          = NULL;
              
   ConnList_LeaveCriticalSection((WX_PTR) pConn);
   return (pClientConn ? WX_SUCCESS : WX_FAILURE );
}
 
/*
 * Remova uma conex�o da lista de conex�es ativas.
 * 03/07/2008 - 08:30:42
 */
WX_RES ConnList_UnRegister( PConnection pConn )
{
   PConnection pClientConn, pNext, pPrevious;
   WX_RES Result = WX_FAILURE;

   HB_TRACE( HB_TR_DEBUG, ("ConnList_UnRegister(%p)", pConn ));
   
   if (!sbStarted) 
      wxConnection_InternalError( WX_ERROR_SUBSYSTEM_INVALID );

   ConnList_EnterCriticalSection("ConnList_UnRegister",(WX_PTR) pConn);
   
   // Sen�o houver nenhuma conex�o, abortamos aqui!
   if (!pConnList)
   {
      HB_TRACE( HB_TR_DEBUG, ("  NAO EXISTE NENHUMA CONEXAO ATIVA!!!! ---> (%p)", pConnList));
      Result = WX_SUCCESS;
      goto FIM;
   }
      
   // H� mais de uma conex�o ativa? Pegue a �ltima!
   pClientConn = pConnList;
   pPrevious   = NULL;           // Item anterior ao atual  
   
   do { 
      
      HB_TRACE( HB_TR_DEBUG, ("pClientConn            -> (%p)", pClientConn ));
      HB_TRACE( HB_TR_DEBUG, ("pClientConn->pNext     -> (%p)", pClientConn->pNext ));
            
      if (pClientConn == pConn)
         break;      
      
      pPrevious   = pClientConn;
      pClientConn = pClientConn->pNext;
            
   } while( pClientConn );
   
   HB_TRACE( HB_TR_DEBUG, ("pClientConn (((ACHEI))  >> %p", pClientConn ));
   
   // Nao achou a conex�o?
   if (!pClientConn)
      goto FIM;
   
   pNext = pConn->pNext;
   
   HB_TRACE( HB_TR_DEBUG, ("pNext     -> (%p)", pNext ));
   
   if (pPrevious)
      pPrevious->pNext = pNext;
   
   pConn->pNext     = NULL;
   
   /*
    * Atualizamos aqui o  primeiro item da lista, caso seja necess�rio pesquisarmos 
    * algo. Infelizmente devido � um erro de digita��o, fiquei 3 dias pra descobrir
    * que a atribui��o abaixo N�O DEVERIA ser de um valor NULL...isto foi corrigido
    * depois de se ler um LOG de 28.432 linhas... Agora t� blz!
    * 14/07/2008 - 12:05:55
    */
   if (pConn == pConnList)
      pConnList = pNext;
      
   Result = WX_SUCCESS;
   FIM:
     ConnList_LeaveCriticalSection((WX_PTR) pConn);
     return Result;
}

/*
 * Puxa a conex�o vinculada � Thread atual 
 * 03/07/2008 - 12:59:51
 */
PConnection wxGetClientConnection( void )
{
   PConnection pClientConn;
   
#if defined( HB_THREAD_SUPPORT )   
   long ThreadID = HB_VM_STACK.th_id;
#else
   long ThreadID = 0L;
#endif   

   //HB_TRACE( HB_TR_DEBUG, ("wxGetClientConnection(%lu)", ThreadID));
  
   if (!sbStarted)
      return NULL;
      
   ConnList_EnterCriticalSection("wxGetClientConnection", (WX_PTR) ThreadID);
      
   pClientConn = pConnList;  

   do { 
      
      if (!pClientConn)
         break;      
      
      if (pClientConn->ThreadID == ThreadID)
         break;
      
      pClientConn = pClientConn->pNext;
   } while( pClientConn );
   
// FIM:
      ConnList_LeaveCriticalSection((WX_PTR) ThreadID );
      return pClientConn;
}


/*
 * Inicializa as variaveis internas que auxiliam no controle de conex�o, sess�o
 * e gerenciamento deste subsistema.
 * 03/07/2008 - 07:30:13
 */
WX_RES wxConnection_Init( void )
{
   char *s;
   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Init()    || %s --> %d", szModuleInit, sbStarted ));

   if (sbStarted)
      return WX_SUCCESS;

   s = wxGetModuleName( WX_PRG_LONG );   
   szModuleInit[0] = '\0';
   xStrMove( szModuleInit, s, NULL );   
   hb_xfree(s);                              
	      
   #ifdef HB_THREAD_SUPPORT
      HB_CRITICAL_INIT( pConnMutex );
   #endif
   
   wxTraceInit();
   
   pConnList = NULL;
   sbStarted = TRUE;
   return WX_SUCCESS;
}

/*
 * Finaliza as variaveis e estruturas utilizadas por este subsistema.
 * 03/07/2008 - 07:34:37
 */
WX_RES wxConnection_Exit( void )
{
   char *s;
   PConnection pClientConn;
   PConnection pNext;
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Exit()    || Init: %s   Exit: %s", szModuleInit, szModuleExit ));

   if (!sbStarted)
      return WX_SUCCESS;
      
   s = wxGetModuleName( WX_PRG_LONG );   
   szModuleExit[0] = '\0';
   xStrMove( szModuleExit, s, NULL );   
   hb_xfree(s);                              
      
   /*
    * Aqui devemos finalizar quaisquer conex�es ainda ativas e pendentes.
    * 26/09/2008 - 12:26:26
    */
   pClientConn = pConnList;
     
   do {       
      HB_TRACE( HB_TR_DEBUG, ("  Destruindo a  conexao     -> (%p) *", ((pClientConn) ? pClientConn->Handle : 0 ), pClientConn ));
      
      if (!pClientConn)
         break;      

      HB_TRACE( HB_TR_DEBUG, ("  # Conexao                 -> (%p) *", pClientConn ));
      HB_TRACE( HB_TR_DEBUG, ("  # Pr�xima conexao sera    -> (%p) *", pClientConn->pNext ));

      pNext = pClientConn->pNext;
      
      wxConnection_Destroy( pClientConn );
      
      HB_TRACE( HB_TR_DEBUG, ("  # Pr�xima conexao sera    -> (%p) *", pNext ));
      
      pClientConn = pNext;
      
   } while( pClientConn );
   
   /*
    * TODO: Finalizar quaisquer conex�es ainda pendentes!!!
    */      
   #ifdef HB_THREAD_SUPPORT
      HB_CRITICAL_DESTROY( pConnMutex );
   #endif   
   
   wxTraceExit();
      
   sbStarted = FALSE;
   return WX_SUCCESS;
}

/*
 * Cria um novo objeto Connection e retorna um ponteiro para a estrutura nova.
 * 02/07/2008 - 22:26:36
 */
PConnection wxConnection_Create( int Handle, char *OutPutFunc )
{
   PConnection pConn;
   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Create(%d,'%s')", Handle, OutPutFunc ));

   // Nao inicializou o subsistema... ignoramos o comando ent�o!
   if (!sbStarted)
      return NULL;
      
   pConn = (PConnection) hb_xgrab( sizeof( TConnection ) );   
   memset( pConn, '\0', sizeof( TConnection ) );
   
   pConn->Handle   = Handle;
#if defined( HB_THREAD_SUPPORT )   
   pConn->ThreadID = HB_VM_STACK.th_id;
#else
   pConn->ThreadID = 0L;
#endif   
      
   /* Se ele passou o nome da fun��o para processar o OutPut, chamamos ela! */
   if (OutPutFunc)
   {
      /* Deu erro de algum tipo? Cancelamos ent�o a cria��o da conex�o */
      if (wxWebRegisterOutputFunc( pConn, OutPutFunc ) == WX_FAILURE )
      {
         wxConnection_Destroy( pConn );
         return NULL;      
      }
   }
      
   /*
    * Se falhou ao inicializar os campos internos, temos que abortar a rotina!
    */
   if (wxConnection_Initialize( pConn ) == WX_FAILURE )
   {
      wxConnection_Destroy( pConn );
      return NULL;
   }   
   
   /*
    * Adicionamos � conex�o atual � listagem de conex�es
    */
   if (ConnList_Register( pConn ) == WX_FAILURE )
   {
      wxConnection_Destroy( pConn );
      return NULL;
   }       
   return pConn;             
}

/*
 * Inicializamos os membros da estrutura passada como argumento
 * 02/07/2008 - 22:35:03
 */
WX_RES wxConnection_Initialize( PConnection pConn )
{                                      
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Initialize(%p)", pConn ));
   if (!pConn)
      return WX_FAILURE;
      
   pConn->ConnType   = mtNone;
   pConn->LastType   = WX_NONE;

   /*
    * Permite capturar a saida de v�deo!
    * 30/06/2008 - 11:31:27
    */
   pConn->ContentType = xStrNewBuff(-1,"Content-type: text/html"); 
   pConn->CacheType   = WX_CACHE_NONE;        // Por padr�o os dados n�o ser�o cacheados para acelerar o desempenho

   return WX_SUCCESS;
}   

/*
 * Ajustamos os COOKIES disponiveis e os adicionamos � conex�o. Ao final do 
 * processo o buffer original em pServer ser� zerado.
 * 19/12/2006 14:59:25
 */
static
WX_RES wxConnection_ParseCookies( PConnection pConn )
{
   PRepositoryItem pItem;
   
   char *key;
   char *buffer;
   char *value;
   ULONG Size;
   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_ParseCookies(%p) || %p ", pConn, pConn->pServer[ HTTP_COOKIE ] ));
   
   /* H� COOKIRES para serem processados? */
   if (!pConn->pServer[ HTTP_COOKIE ])
      return WX_SUCCESS;
      
   buffer = (char *)pConn->pServer[ HTTP_COOKIE ]->Value;
   key    = strtok( buffer, "; \t\f\r\n,");
      
	while (key!=NULL)
	{
	   value = strrchr( key, '=' );
	   
	   if (!value)
	      break;
          
      pItem  = RepositoryList_Create();         
      pItem->Key = xStrUpperNew( key, value - key );
      
      value ++;
      Size = strlen( value );
      
      RepositoryList_UpdateStr( pItem, (BYTE *) value, Size );
            
      			HB_TRACE( HB_TR_DEBUG, ("  >>>> COOKIES :: pItem->Key   -> %s	"	, pItem->Key ));
      			HB_TRACE( HB_TR_DEBUG, ("  >>>> COOKIES :: pItem->Value -> %s	"	, pItem->Value ));

      /* Ajustamos � pilha para refletir o item correto */
      if (pConn->pCookies)
         pItem->pNext = pConn->pCookies;
         
      pConn->pCookies = pItem;
      pConn->CookieCount ++;
      
		key=strtok(NULL, "; \t\f\r\n,");
	}		
   return WX_SUCCESS;   	
}

/*
 * Processa os valores recebidos via FORM quando o usuario tenta enviar algum
 * arquivo diretamente da p�gina para a nossa aplica��o!
 * 07/11/2008 - 10:39:50
 *
 * Referencias: 
 *    http://www.faqs.org/rfcs/rfc1867.html
 *    http://www.w3.org/TR/html401/interact/forms.html#h-17.13.4.1
 *    http://www.w3.org/TR/html401/references.html#ref-RFC2388
 *    http://www.cs.tut.fi/~jkorpela/forms/file.html
 */
/*

The global $_FILES exists as of PHP 4.1.0 (Use $HTTP_POST_FILES instead if using an earlier version). These arrays will contain all the uploaded file information. 

The contents of $_FILES from the example form is as follows. Note that this assumes the use of the file upload name userfile, as used in the example script above. This can be any name. 

$_FILES['userfile']['name']
The original name of the file on the client machine. 

$_FILES['userfile']['type']
The mime type of the file, if the browser provided this information. An example would be "image/gif". This mime type is however not checked on the PHP side and therefore don't take its value for granted. 

$_FILES['userfile']['size']
The size, in bytes, of the uploaded file. 

$_FILES['userfile']['tmp_name']
The temporary filename of the file in which the uploaded file was stored on the server. 

$_FILES['userfile']['error']
The error code associated with this file upload. This element was added in PHP 4.2.0 

Files will, by default be stored in the server's default temporary directory, unless another location has been given with the upload_tmp_dir directive in php.ini. The server's default directory can be changed by setting the environment variable TMPDIR in the environment in which PHP runs. Setting it using putenv() from within a PHP script will not work. This environment variable can also be used to make sure that other operations are working on uploaded files, as well. 

*/
static
WX_RES wxConnection_ParseMultipart( PConnection pConn, char *Buffer, ULONG Length )
{
   char Field[MAX_FIELD_LEN+1];
   char *pos, *end;
   char *boundary;
   char *filename;
   char *name;
   char *content;
   char *value;
   ULONG value_len;
   int boundary_len;
   int L;                                    

   HB_TRACE( HB_TR_DEBUG, ("wxConnection_ParseMultipart(%p) || %p ", pConn, pConn->pServer[ HTTP_CONTENT_TYPE ] ));
   
   if (!Buffer)
      return WX_SUCCESS;

   /*
    * NOTE que eu nao valido nada aqui, pois a rotina que me chama J� DEVE previamente 
    * ter feito antes e sendo assim, vou evitar perder tempo com isto.
    */
   boundary  = xStrDup( (char *) pConn->pServer[ HTTP_CONTENT_TYPE ]->Value );   
   boundary  = strstr( boundary, "boundary" );   
   
   /* Se nao tem a assinatura valida, pulamos fora! */
   if (!boundary)
      // ERROR: "Missing boundary in multipart/form-data POST data"
      goto fim;

   if (!(boundary=strchr(boundary, '=')))
      // ERROR: "Missing boundary in multipart/form-data POST data"
      goto fim;
   
   if (pos = strchr( boundary, ';' )) 
      *pos = '\0';  

   if (boundary[0] == '"' )
   { 
      boundary ++;
      if (pos = strchr( boundary, '"' )) 
         *pos = '\0';  
   }  
           
   /* Formatamos o boundary e pegamos o seu length */
   boundary --;
   boundary[0] = '-'; boundary[1] = '-';
   boundary_len= strlen( boundary );   
   
   HB_TRACE( HB_TR_DEBUG, ("  >>>> boundary   -> %s <<<<	"	, boundary ));
   
   /* Guardamos para referencia aqui, qual � a posicao do ultimo item na string*/
   end = &Buffer[ Length-1 ];
     
   /* Fa�a enquanto n�o achar um #0 dentro da string */
   while (*Buffer)
   {         
      /* Tem que haver qtde suficiente de bytes no buffer */
      if ( !(Length >= boundary_len ) )
         break;         

      /* Tem a assinatura v�lida? Senao tiver, j� p�ra por aqui mesmo */
      if ( strncmp( Buffer, boundary, boundary_len ) != 00 )
         break;
            
      /* Saltamos os bytes j� confirmados e ficamos na proxima posicao para analise */
      Buffer  += boundary_len;
      Length  -= boundary_len;
      
      /* Aqui testamos se � fim da string, i.e. boundary + '--' */
      if ( Buffer[0] == '-' && Buffer[1] == '-' )
         break;
         
      /*
       * Tentamos localizar as 2 quebras de linhas que identificam o inicio do 
       * conteudo do arquivo ou campo recebido.. nao existem? Entao pula fora!
       */       
      pos = strstr( Buffer, "\r\n\r\n" );
      
      if (!pos)
         break;
         
      *pos = '\0';
      filename = strstr( Buffer, " filename=");
      name     = strstr( Buffer, " name=");
		content  = strstr( Buffer, "Content-Type: ");
      value    = pos +4;      
      
      /* Aqui ajustamos alguns parametros recebidos */
      if (filename)
      {
         filename += strlen( " filename=");         
         if (*filename == '"')
         {
            filename ++;
            pos = strchr( filename, '"' );
            if (pos) *pos = '\0';
         }         
      }
      if (name)
      {
         name += strlen( " name=");         
         if (*name == '"')
         {
            name ++;
            pos = strchr( name, '"' );
            if (pos) *pos = '\0';
         }         
      }
      if (content)
         content += strlen( "Content-Type: ");         
      
   // HB_TRACE( HB_TR_DEBUG, (" File Header --> [%s]", Buffer ));
      HB_TRACE( HB_TR_DEBUG, ("  filename   --> [%s]", filename ));
      HB_TRACE( HB_TR_DEBUG, ("     field   --> [%s]", name ));
      HB_TRACE( HB_TR_DEBUG, ("   content   --> [%s]", content ));         
         
      /* Posicionamos o ponteiro para o proximo campo passado como argumento */
      Buffer = wxMemFind( Buffer, Length, boundary, boundary_len );
      
      if (!Buffer)
         break;
         
      value_len = (Buffer - value) -2; /* tiramos 2 bytes por causa do CRLF */
         
      HB_TRACE( HB_TR_DEBUG, ("     value_len > [%lu]", value_len ));
   // HB_TRACE( HB_TR_DEBUG, ("     value   --> [%s]", value ));
      HB_TRACE( HB_TR_DEBUG, (""));
      
      /* Adicionamos ela a lista de parametros e se algo der errado, pulamos fora! */
      if ( wxFieldAdd( pConn, name, value, value_len, FALSE )  == WX_FAILURE )
          break;
                              
      Length = end - Buffer;
   }
   fim:
   
      if (boundary)
          wxDispose( boundary );
   
   return WX_SUCCESS ;
}
 
/*
 * Processa os valores atribuidos �s variaveis de ambiente desta conex�o. Sendo
 * que estes parametros podem ser via QUERY_STRING e o conteudo de POST (nao
 * URL-ENCODED)...
 * 19/12/2006 14:59:25
 */
static
WX_RES wxConnection_ParseBuffer( PConnection pConn, PRepositoryItem pItem, BOOL bCheckParseData )
{
   char *Buffer;
   char Field[MAX_FIELD_LEN+1];
   char *pos;
   int L;                                    
   ULONG Size;
      
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_ParseBuffer(%p,%p,%d)", pConn, Buffer, bCheckParseData ));

   // 23/07/2008 - 17:46:51 - H� algo para processarmos?
   if (!pItem)
      return WX_SUCCESS;
      
   Buffer = (char *) pItem->Value;
             
   HB_TRACE( HB_TR_DEBUG, ("  Buffer --> '%s'", Buffer ));
   
   /*
    * Aqui testamos se ele passou o formulario com URL-ENCODED e sendo assim, 
    * devemos mandar ele para outra funcao espec�fica!
    * 07/11/2008 - 09:04:18
    */
   if (bCheckParseData                          &&       // Validamos se nao tem algum arquivo em anexo
       pConn->pServer[ HTTP_CONTENT_TYPE ]      &&       // Validamos se o content-type � valido
       pConn->pServer[ HTTP_CONTENT_TYPE ]->Value )      // Validamos se o texto do "content-type" � valido
   {
      char *content_type = (char *) pConn->pServer[ HTTP_CONTENT_TYPE ]->Value;
        
      /* Testamos se possui a assinatura correta no caso de envio de arquivos */ 
      if (strstr( content_type, "multipart/form-data;"))
      {
         HB_TRACE( HB_TR_DEBUG, ("  Recebemos arquivos!! -> %s", content_type ));
         return wxConnection_ParseMultipart( pConn, Buffer, pItem->Len );
      }   
      
      HB_TRACE( HB_TR_DEBUG, ("  NAO Recebemos NENHUM TIPO DE arquivo!! -> %s", content_type ));
   }

   while (*Buffer)
   {      
      pos = strchr(Buffer,'='); 
    
      if (!pos)
         break;
      
      L = (pos)-(Buffer);
      
      if (L>MAX_FIELD_LEN)
         L = MAX_FIELD_LEN;
      
      /* Pegamos o nome do campo */
      memmove( Field, Buffer, L );
      Field[L] = '\0';

      Buffer = pos;
      Buffer ++;
      
//HB_TRACE( HB_TR_DEBUG, ("NAME:  %d   || %s", L, Field ));

      /* Puxamos o valor */      
      pos = strchr( Buffer, '&' );
      
      if (pos)
         Size = (pos) - Buffer;
      else 
         Size = strlen( Buffer );

//HB_TRACE( HB_TR_DEBUG, ("VALUE: %lu  || %s", Size, Buffer ));
      /* Adicionamos o campo � conex�o atual */
      if ( wxFieldAdd( pConn, Field, Buffer, Size, TRUE ) == WX_FAILURE )
         return WX_FAILURE;
      
      Buffer += Size;   
      if (pos)
         Buffer ++;
      
   }
   return WX_SUCCESS ;
}

/*
 * Processa os valores atribuidos �s variaveis de ambiente desta conex�o.
 * 19/07/2008 - 09:39:38
 */
WX_RES wxConnection_Prepare( PConnection pConn )
{   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Prepare(%p)", pConn ));

   /* Avisamos ao TRIGGER que a conexao est� sendo iniciada! 15/07/08 - 22:10:30 */
   if ( pConn->pState && pConn->pState( pConn, cfInit, NULL ) == WX_FAILURE )
      return WX_FAILURE;

   /* Separamos os campos recebidos inicialmente via GET */
   if ( wxConnection_ParseBuffer( pConn, pConn->pServer[ QUERY_STRING ], FALSE ) == WX_FAILURE )
      return WX_FAILURE;

   /* Separamos agora os campos recebidos via POST */
   if ( wxConnection_ParseBuffer( pConn, pConn->pServer[ HTTP_CONTENT_BUFFER ], TRUE ) == WX_FAILURE )
      return WX_FAILURE;
                                                     
   // Aqui zeramos o conteudo do POST para evitar ocupar memoria desnecess�ria
   RepositoryList_Finalize( pConn->pServer[ HTTP_CONTENT_BUFFER ] );

   /* Ajustamos os COOKIES disponiveis */
   if ( wxConnection_ParseCookies( pConn ) == WX_FAILURE )
      return WX_FAILURE;
      
   return WX_SUCCESS;
}

/**
 * wxConnection_Prepare( <pConn> ) -> lResult
 *
 * Processa todos os valores brutos atrelados �s variaveis internas de ambiente
 * da conex�o passada como argumento, � fim de torn�-los dispon�veis �s demais
 * rotinas da wxWeb. Normalmente esta fun��o � utilizada internamente para atuar
 * da nos dados recebidos tais como coookies, valores recebidos via GET/POST/PUT, etc.
 * 23/07/2008 - 16:29:36
 */
HB_FUNC( WXCONNECTION_PREPARE )
{
   if (!ISPOINTER(1))
      return;   
   
   if (wxConnection_Prepare( (PConnection) hb_parptr(1) ) == WX_SUCCESS)
      hb_retl( TRUE );
   else
      hb_retl( FALSE );
} 

/*
 * Destruimos a estrutura atual, mas antes resolveremos alguns detalhes
 * 02/07/2008 - 22:37:27
 */
WX_RES wxConnection_Destroy( PConnection pConn )
{
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Destroy(%p)", pConn ));

   if (!pConn)
      return WX_FAILURE;
                            
   /*
    * Avisamos ao TRIGGER que a conex�o est� prestes � ser destruida. Se for o
    * caso, ele possa operar sobre os dados antes deles serem zerados, como por
    * exemplo: capturar todo o buffer de saida antes dele ser impresso!
    * 15/07/2008 - 22:08:48
    */
   if (pConn->pState)
      pConn->pState( pConn, ctBeforeFinalize, NULL );   

   wxConnection_Print( pConn );
   wxConnection_Finalize( pConn ); 
   ConnList_UnRegister( pConn );

   /* Avisamos que a conex�o vai ser destruida agora! */
   if (pConn->pState)
      pConn->pState( pConn, ctAfterFinalize, NULL );   

   hb_xfree( (WX_PTR) pConn );
   return WX_SUCCESS;
}

/*
 * Destruimos os campos descart�veis desta estrutura
 * 02/07/2008 - 22:38:53
 */
WX_RES wxConnection_Finalize( PConnection pConn )
{
   PRepositoryItem pItem, pNext;
   int I;
   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Finalize(%p)", pConn ));
   
   if (!pConn)
      return WX_FAILURE;
   
   /*
    * Destruimos aqui todas as variaveis de Sessao mas ANTES iremos gravar quaisquer
    * dados diretos no HD.
    * 01/08/2008 - 22:18:21
    */
   wxSession_Write( pConn );
   wxSession_Clear( pConn, FALSE );
   
   /*
    * Apagamos quais dados desnecessarios - 30/06/2008 - 11:36:14 
    */
   if (pConn->OutPutFunc)
   {
      hb_xfree(pConn->OutPutFunc);
      pConn->OutPutFunc = NULL;      
   }
   if (pConn->ContentType)
   {
      hb_xfree(pConn->ContentType);
      pConn->ContentType = NULL;      
   }
   if(pConn->ErrorMsg)
   {
      hb_xfree( pConn->ErrorMsg );
      pConn->ErrorMsg = NULL;
   }
   if (pConn->OutPutStartedAt)
   {
      hb_xfree( pConn->OutPutStartedAt );
      pConn->OutPutStartedAt = NULL;
   }

   /*
    * Despachamos todos os cabe�alhos do cache vinculados � esta conexao!
    */         
   pItem = pConn->HeaderFirst;
   
   while (pItem)
   {
      pNext = pItem->pNext;
      RepositoryList_Destroy( pItem );
      pItem = pNext;
   }         
   pConn->HeaderFirst = NULL;
   pConn->HeaderLast  = NULL;

   /*
    * Despachamos todos os itens do BODY deste documento armazenados no cache 
    * desta conexao!
    */         
   pItem = pConn->BodyFirst;
   
   while (pItem)
   {
      pNext = pItem->pNext;
      RepositoryList_Destroy( pItem );
      pItem = pNext;
   }      
   pConn->BodyFirst = NULL;
   pConn->BodyLast  = NULL;

   /* Variaveis do servidor */
      for (I=0; I < pConn->ServerCount; I++)
      {
         if (!pConn->pServer[I])
            continue;
         RepositoryList_Destroy( pConn->pServer[I] );
      }
      if (pConn->pServer)
      {
         hb_xfree(pConn->pServer);
         pConn->pServer = NULL;
      }
      
      pConn->ServerCount = 0;

   /* Aqui destruimos os campos recebidos via GET/POST/PUT */
   pItem = pConn->pFields;   
   while (pItem)
   {
      pNext = pItem->pNext;
      RepositoryList_Destroy( pItem );
      pItem = pNext;
   }       
   pConn->pFields = NULL;  
   
   /* Aqui destruimos as configura��es carregadas em memoria */
   pItem = pConn->pConfig;   
   while (pItem)
   {
      pNext = pItem->pNext;
      RepositoryList_Destroy( pItem );
      pItem = pNext;
   }       
   pConn->pConfig = NULL;  
   
   /* Destruimos agora os COOKIES anexados � esta conex�o */
   pItem = pConn->pCookies;   
   while (pItem)
   {
      pNext = pItem->pNext;
      RepositoryList_Destroy( pItem );
      pItem = pNext;
   }       
   pConn->pCookies = NULL;  
   
   HB_TRACE( HB_TR_DEBUG, ("  Ok! Finished cleanup pConn->->(%p)", pConn ));
   return WX_SUCCESS;
}

/*
 * Encaminha o texto passado como argumento para o OutPut
 * 02/07/2008 - 22:38:28
 */
static 
WX_RES wxConnection_WriteOutput( PConnection pConn, BYTE Type, BYTE *Text, ULONG Size )
{   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_WriteOutput(%p, %d, '%s', %lu)  into '%s' (%p/%p) ", 
                              pConn, Type, Text, Size, 
                              (pConn) ? pConn->OutPutFunc : 00, 
                              (pConn) ? pConn->OutPutSymbol : 00, 
                              (pConn) ? pConn->pOutPut : 00));
      
   if (!pConn)
      return WX_FAILURE;
   if (!pConn->pOutPut)
      return WX_FAILURE;

   if ((Type == WX_HEADER) &&
       (pConn->LastType == WX_BODY ))
   {
      HB_TRACE( HB_TR_DEBUG, ("     --> headers already been sent (%d)!", pConn->LastType )) 
      return WX_FAILURE;
   }   
      
   if (!Text) return WX_SUCCESS;
   if (!Size) return WX_SUCCESS;
   
   if (pConn->pOutPut( pConn, Text, Size ) != WX_SUCCESS)
      return WX_FAILURE;

   pConn->LastType = Type;       
   return WX_SUCCESS;
}

/*
 * Envia o conteudo do HEADER / BODY para o OutPut, numa unica string formatada
 * com todo o conteudo.
 * 23/07/2008 - 09:52:24
 */
static
WX_RES wxConnection_PrintFullCache( PConnection pConn )
{
   PRepositoryItem pItem, pNext;
   BYTE *Buffer, *Pointer;
   ULONG Length;
   WX_RES Result;
   BOOL bCRLF;

   HB_TRACE( HB_TR_DEBUG, ("wxConnection_PrintFullCache(%p)", pConn ));
   
   /* 1� Passo: Computamos o tamanho total necess�rio para este buffer */
   pItem = pConn->HeaderFirst;
   Length= 0L;

   bCRLF = (( pConn->HeaderFirst && !pConn->BodyFirst ) ||     // Tem HEADER mas nao tem BODY 
            (!pConn->HeaderFirst && !pConn->BodyFirst ) ||     // Nao tem HEADER e nem BODY
            ( pConn->LastType != WX_BODY ));                   // Acho q s� isto bastava... se nao tiver texto..

   while (pItem)
   {
      Length += pItem->Len;
      pItem   = pItem->pNext;
   }
   
   pItem = pConn->BodyFirst;

   while (pItem)
   {
      Length += pItem->Len;
      pItem   = pItem->pNext;
   }
      
   if (bCRLF)
      Length += 4;
   
   /* 2� Passo: Preparamos o Buffer de dados */
   Buffer = (BYTE *) hb_xgrab( Length +1 );
   Pointer= Buffer;
   
   Buffer[Length] = '\0';
   
   /* 3� Passo: Movemos os dados para a regi�o espec�fica da memoria */
   HB_TRACE( HB_TR_DEBUG, ("     pConn->HeaderFirst -> %p", pConn->HeaderFirst ));
   pItem = pConn->HeaderFirst;

   while (pItem)
   {
      pNext = pItem->pNext;

      /*
       * Movemos ele para o nosso buffer e destruimos ele da pilha para liberar 
       * RAM. 
       */
      memcpy( Buffer, pItem->Value, pItem->Len );
      Buffer += pItem->Len;
      
      if (pConn->HeaderLast == pItem)
         pConn->HeaderLast = pNext;

      RepositoryList_Destroy( pItem );

      pItem = pNext;
      pConn->HeaderFirst = pItem;      

   }      

   // 27/10/2008 - 21:55:49
   if (bCRLF)
   {
      *Buffer = '\r'; Buffer ++; *Buffer = '\n'; Buffer ++;
      *Buffer = '\r'; Buffer ++; *Buffer = '\n'; Buffer ++;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("     pConn->BodyFirst -> %p", pConn->BodyFirst ));
   pItem = pConn->BodyFirst;

   while (pItem)
   {
      pNext = pItem->pNext;

      /*
       * Movemos ele para o nosso buffer e destruimos ele da pilha para liberar 
       * RAM. 
       */
      memcpy( Buffer, pItem->Value, pItem->Len );
      Buffer += pItem->Len;
         
      if (pConn->BodyLast == pItem)
         pConn->BodyLast = pNext;

      RepositoryList_Destroy( pItem );

      pItem = pNext;
      pConn->BodyFirst = pItem;      
   }      
   
   /* 4� Passo: Enviar o buffer com o resultado da conex�o para o usu�rio */ 
   Result = wxConnection_WriteOutput( pConn, WX_BODY, Pointer, Length );
   
   /* 5� Passo: Liberamos a mem�ria temporaria e retornamos */
   hb_xfree( Pointer );
   return Result;
}

/*
 * Envia o conteudo de HEADER e BODY para o OutPut
 * 02/07/2008 - 22:38:28
 */
WX_RES wxConnection_Print( PConnection pConn )
{
   PRepositoryItem pItem, pNext;
   BOOL bCRLF;
   
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_Print(%p)  || pConn->pOutPut -> %p", pConn, ((pConn) ? pConn->pOutPut : NULL )));
   
   if (!pConn)
      return WX_FAILURE;
   if (!pConn->pOutPut)
      return WX_FAILURE;

   /* Checamos o tipo de cache aplicados � conex�es - 23/07/2008 - 09:58:37 */ 
   switch (pConn->CacheType)
   {
      case WX_CACHE_NONE   :     /* N�o usamos cache algum */
      case WX_CACHE_DEFAULT:     /* Os dados ser�o cacheados e enviados somente neste ponto de execu��o */
           break;
           
      case WX_CACHE_FULL   :     /* Ok, moveremos os dados num buffer termpor�rio e ent�o o dispachamos para o cliente */
           wxConnection_PrintFullCache( pConn );
           break;  
   }
   
   /*
    * Despachamos todos os cabe�alhos do cache vinculados � esta conexao!
    */   
   HB_TRACE( HB_TR_DEBUG, ("     pConn->HeaderFirst -> %p", pConn->HeaderFirst ));
   pItem = pConn->HeaderFirst;
   /*
   bCRLF =  (( pConn->HeaderFirst && !pConn->BodyFirst ) ||     // Tem HEADER mas nao tem BODY 
            (!pConn->HeaderFirst && !pConn->BodyFirst ) ||     // Nao tem HEADER e nem BODY
            ( pConn->LastType != WX_BODY ));                   // Acho q s� isto bastava... se nao tiver texto..
            */
   bCRLF =  (pConn->LastType != WX_BODY );                     // Acho q s� isto bastava... se nao tiver texto.. 
   
   while (pItem)
   {
      pNext = pItem->pNext;

      if (wxConnection_WriteOutput( pConn, WX_HEADER, (BYTE *) pItem->Value, pItem->Len ) == WX_FAILURE)
         return WX_FAILURE;
         
      if (pConn->HeaderLast == pItem)
         pConn->HeaderLast = pNext;

      RepositoryList_Destroy( pItem );

      pItem = pNext;
      pConn->HeaderFirst = pItem;      
   }      
   
   // 27/10/2008 - 21:55:54
   if (bCRLF)
      wxConnection_SendText( pConn, (BYTE *) "\r\n", 2 );

   /*
    * Despachamos todos os itens que compoe o BODY deste documento armazenado no
    * cache deste desta conexao!
    */   
   HB_TRACE( HB_TR_DEBUG, ("     pConn->BodyFirst -> %p", pConn->BodyFirst ));
   pItem = pConn->BodyFirst;

   while (pItem)
   {
      pNext = pItem->pNext;

      if (wxConnection_WriteOutput( pConn, WX_BODY, (BYTE *) pItem->Value, pItem->Len ) == WX_FAILURE)
         return WX_FAILURE;
         
      if (pConn->BodyLast == pItem)
         pConn->BodyLast = pNext;

      RepositoryList_Destroy( pItem );

      pItem = pNext;
      pConn->BodyFirst = pItem;      
   }      
   return WX_SUCCESS;
}

/*
 *
 * 15/07/2008 - 10:54:06
 */                      
static
WX_RES wxConnection_Signature( PConnection pConn )
{
   //HB_TRACE( HB_TR_DEBUG, ("  wxConnection_Signature(%p)", pConn ));
   /*
    * Enviamos a assinatura do projeto para o cliente.
    *
    * Mas note que neste caso, se os dados n�o forem cacheados e ainda n�o houver
    * sido setada nenhuma fun��o para tratamento do OutPut a linha abaixo ser�
    * perdida e nada ser� enviado para o cliente!!!  
    * 08/07/2008 - 08:13:03
    */ 
   if ((pConn->LastType == WX_NONE) &&
       (pConn->pOutPut))
   {
#ifndef VERSAO_COMERCIAL
     char StrPowered[80] = {0};   
     char Expires[30]    = {0};
            
     int m[] = WX_EXPIRES;
         
     sprintf( Expires, " / Expires: %04d-%02d-%02d",              
                                        (m[0]+1950),
                                        (m[1]-30)  ,
                                        (m[2]-100));
#else
     char StrPowered[50] = {0};   
     char Expires[] = {0}; 
#endif       
      sprintf( StrPowered, "X-Powered-By: %s Framework v%s%s", XWEB_NAME, WXWEB_VERSION, Expires );
      
      pConn->LastType = WX_HEADER;
      wxConnection_SendHeader( pConn, StrPowered, strlen( StrPowered) );      
   }
   return WX_SUCCESS;
}
 
/*
 * Enviamos o texto passado como argumento para o client.
 * 03/07/2008 - 18:03:23
 */ 
WX_RES wxConnection_SendHeader( PConnection pConn, BYTE *Text, ULONG Size )
{
   PRepositoryItem pItem;
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_SendHeader(%p, '%p', %lu)", pConn, Text, Size ));

   // Sem conex�o ou texto valido? Indicamos erro!
   if ((!pConn) || (!Text))
      return WX_FAILURE;
   
   /* Ops... ele est� mandando um HEADER, porem j� iniciamos o BODY da p�gina? */
   if (pConn->LastType == WX_BODY) 
   {                          
      /*
       * Aqui avisamos para o usuario, que o buffer de saida j� fora inicializado!
       * 21/07/2008 - 14:31:33
       */   
		if (pConn->OutPutStartedAt)
      {
         char Text[255] = {0};
			sprintf( Text, "WARNING: Cannot send text - headers already sent (output started at %s)", pConn->OutPutStartedAt );
			wxConnection_SendText( pConn, Text, strlen( Text ) );
		} else {
			char text[] = {"WARNING: Cannot send text - headers already sent"};
			
			wxConnection_SendText( pConn, Text, strlen( Text ) );
		}
      return WX_FAILURE;
   }
      
   if (pConn->LastType == WX_NONE)
      wxConnection_Signature( pConn );

   pConn->LastType = WX_HEADER;
   
   /*
    * Enviamos o texto diretamente para os devidos CANAIS, visto que nao devemos
    * utilizar o sistema de cache da wxWeb
    * 03/07/2008 - 17:31:56
    */
   if (pConn->CacheType == WX_CACHE_NONE)               
   {  
      WX_RES Result = wxConnection_WriteOutput( pConn, WX_HEADER, Text, Size );
      
      // Enviamos uma quebra de linha obrigat�ria para os cabe�alhos! 08/07/2008 - 09:00:24
      if (Result == WX_SUCCESS)
         Result = wxConnection_WriteOutput( pConn, WX_HEADER, (BYTE *) "\r\n", 2 );
      
      return Result;
   }
   /*
    * Armazenamos os dados solicitados no CACHE
    */
   HB_TRACE( HB_TR_DEBUG, ("  CACHING HEADER" ));
   pItem = RepositoryList_AddStr( pConn->HeaderLast, Text, Size, TRUE );
   
   if (!pConn->HeaderFirst)
        pConn->HeaderFirst = pItem;
   
   pConn->HeaderLast = pItem;
   pConn->LastType   = WX_HEADER;      // BUG: Tinha que setar aqui tb!!!! 02/08/2008 - 12:46:37       

   return WX_SUCCESS;
}

/*
 * Enviamos o texto passado como argumento para o client
 * 03/07/2008 - 13:16:05
 */ 
WX_RES wxConnection_SendText( PConnection pConn, BYTE *Text, ULONG Size )
{
   PRepositoryItem pItem;
   HB_TRACE( HB_TR_DEBUG, ("wxConnection_SendText(%p, %lu) || pConn->LastType = %d", pConn, Size, ((pConn) ? pConn->LastType : 999 ) ));
   
   // Sem conex�o ou texto valido? Indicamos erro!
   if ((!pConn) || (!Text))
      return WX_FAILURE;
   /*
    * Sem texto de tamanho valido? Entao indicamos sucesso pois pode ser uma
    * string vazia e neste caso, est� td ok.
    */
   if (Size<1)    
      return WX_SUCCESS;
   if (pConn->LastType == WX_NONE)
      wxConnection_Signature( pConn );      
   /*
    * Enviamos o content-type do documento ANTES de enviarmos quaisquer dados.
    * 08/07/2008 - 07:47:51 
    */   
   if ((pConn->LastType == WX_HEADER) &&
       (pConn->ContentType))
   {
      /*
       * Enviamos o content-type em baixo n�vel para evitar problemas com as
       * rotinas de bufferiza��o de dados e aproveitamos e enviamos uma string
       * vazia, for�ando assim a cria��o de uma linha em branco para separar a 
       * a parte dos cabe�alhos e do corpo da p�gina! 
       * 21/07/2008 - 08:40:28
       */
      if (wxConnection_SendHeader( pConn, (BYTE *) pConn->ContentType, strlen( pConn->ContentType ) ) == WX_FAILURE) 
         return WX_FAILURE;         
      if (wxConnection_SendHeader( pConn, (BYTE *) "", 0 ) == WX_FAILURE)
         return WX_FAILURE;

      hb_xfree(pConn->ContentType);
      pConn->ContentType = NULL;
      
      /*
       * Guardamos aqui o nome da fun��o que iniciou saida dos dados para a tela
       * 21/07/2008 - 11:32:39
       */
      if (!pConn->OutPutStartedAt)
         pConn->OutPutStartedAt = wxGetModuleName( WX_PRG_LONG );
   }
      
   /*
    * Enviamos o texto diretamente para os devidos CANAIS, visto que nao devemos
    * utilizar o sistema de cache da wxWeb
    * 03/07/2008 - 17:31:56
    */
   if (pConn->CacheType == WX_CACHE_NONE)
      return wxConnection_WriteOutput( pConn, WX_BODY, Text, Size );
   
   HB_TRACE( HB_TR_DEBUG, ("  CACHING DATA" ));
   /*
    * Armazenamos os dados solicitados no CACHE
    */
   pItem = RepositoryList_AddStr( pConn->BodyLast, Text, Size, FALSE );
   
   if (!pConn->BodyFirst)
        pConn->BodyFirst = pItem;
   
   pConn->BodyLast = pItem;
   pConn->LastType = WX_BODY;      // BUG: Tinha que setar aqui tb!!!! - 02/08/2008 - 12:46:15       

   return WX_SUCCESS;
}

/**
 * wxConnection_Init() -> nResult
 *
 * Inicializa as vari�veis internas que auxiliam no controle de conex�o, sess�o
 * e gerenciamento deste subsistema. Esta fun��o retornar� um WX_RESULT indicando
 * �xito ou n�o durante este processo.
 * 02/07/2008 - 22:52:33
 *
 * @see @request(CONN_FUNC_LIST)
 */
HB_FUNC( WXCONNECTION_INIT )
{
   hb_retni( wxConnection_Init() );
}

/**
 * wxConnection_Exit() -> nResult
 *
 * Finaliza as variaveis e estruturas utilizadas por este subsistema. Esta fun��o
 * retornar� um WX_RESULT indicando �xito ou n�o durante este processo.
 * 02/07/2008 - 22:52:33
 *
 * @see @request(CONN_FUNC_LIST)
 */
HB_FUNC( WXCONNECTION_EXIT )
{
   hb_retni( wxConnection_Exit() );
}

/**
 * wxConnection_CacheType( [ <pConn>, ] [<nType>] ) -> nOldType
 *
 * Retorna e opcionalmente seta o modelo atual de cache ativo para a conex�o
 * passada como argumento.
 * 23/07/2008 - 09:18:17
 *
 * @<pConn>    O pointer para a conex�o a ser consultada ou alterada.
 * @<nType>    O tipo de cache � ser aplicado � conex�o passada como argumento. Os
 *             poss�veis valores para este argumento s�o:
 * <code>
 * WX_CACHE_NONE     Nenhum cache ser� aplicado � conex�o.
 * WX_CACHE_DEFAULT  Os dados ser�o cacheados e enviados somente neste ponto de execu��o
 * WX_CACHE_FULL     Toda a saida gerada ser� armazenada em cache e enviada ao usu�rio apenas
 *                   no t�rmino da execu��o do script.
 * </code>
 *
 * @see @request(CONN_FUNC_LIST)
 */
HB_FUNC( WXCONNECTION_CACHETYPE )
{
   PConnection pClientConn;
   int i;
      
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }

   HB_TRACE( HB_TR_DEBUG, ("wxConnection_CacheType(%p, %d) ||", pClientConn, (ISNUM(i) ? hb_parni(i) : 999) ));

   if (!pClientConn)
      hb_retni( 0 );
   else
      hb_retni( pClientConn->CacheType );
      
   if (!ISNUM(i))
      return;                  
      
   i = hb_parni(i);
   
   // O valor est� dentro da faixa esperada?
   if (!((i >= WX_CACHE_NONE) && 
         (i <= WX_CACHE_FULL)))
      return;
      
   HB_TRACE( HB_TR_DEBUG, ("  SET CACHE TYPE TO %d", i ));      
   pClientConn->CacheType = i;
   return;
}

/**
 * wxConnection_Create( <nHandle>, <cOutPutFunc> ) -> pConn
 *
 * Cria uma nova conex�o e inicializa  os  seus valores default.
 * 08/07/2008 - 08:36:40
 *
 * @<nHandle>  Um valor  numerico (Handle) para  identificar a conex�o atual dentro
 *             da pilha de conex�es gerenciadas pela wxWeb.
 *
 * @<cOutPutFunc> Uma string contendo o nome da fun��o que ir� operar sobre toda
 *                saida de tela que ocorrer durante o per�odo que esta conex�o
 *                estiver ativa.
 *
 * @see @request(CONN_FUNC_LIST)
 */
HB_FUNC( WXCONNECTION_CREATE ) 
{
   PConnection pConn;

   int   Handle = hb_parni(1);
   char *Output = hb_parcx(2);
   
   /* Criamos a conex�o com o Handle especifico */
   pConn = wxConnection_Create( Handle, Output );
   hb_retptr( (WX_PTR) pConn );
}

/**
 * wxConnection_Destroy( <pConn> ) -> nil
 *
 * Destr�i uma conex�o previamente criada com wxConnection_Create(). Esta fun��o
 * envia quaisquer dados pendentes para a fun��o de saida e libera os recursos de
 * sistema atrelados � esta conex�o.
 * 02/07/2008 - 22:37:27
 *
 * @<pConn>    O pointer para a conex�o a ser destruida.
 *
 * @see @request(CONN_FUNC_LIST)
 */
HB_FUNC( WXCONNECTION_DESTROY )
{
   if (!ISPOINTER(1))
      return;   
   wxConnection_Destroy( (PConnection) hb_parptr(1) );
}

/****************************************************************************/
/* Format items for output, then call output function */
static 
WX_RES wxWebConOut( PConnection pConn, USHORT uiParam )
{
   char * pszString;
   ULONG ulLen;
   BOOL bFreeReq;
   WX_RES Result;

   PHB_ITEM pItem = hb_param( uiParam, HB_IT_ANY );

   if( HB_IS_LOGICAL( pItem ) )
   {
      ulLen = 3;
      bFreeReq = FALSE;
      pszString = ( char * ) ( hb_itemGetL( pItem ) ? ".T." : ".F." );
   }
   else
   {
      pszString = hb_itemString( pItem, &ulLen, &bFreeReq );
   }

   if ( ulLen )
      Result = wxConnection_SendText( pConn, (BYTE *) pszString, ulLen );

   if( bFreeReq )
      hb_xfree( pszString );
   
   return Result;
}

/****************************************************************************/
static 
void WXQQOutAux( BOOL HasPtr, BOOL lSendCRLF ) /* writes a list of values to the client */ 
{
   PConnection pClientConn;
   WX_RES Result = WX_FAILURE;
   USHORT iPCount, iParam;

   /*
    * Validamos aqui se estamos recebendo o ponteiro da conex�o como 1� argumento
    * ou se devemos busca-lo na lista de argumentos;
    * 03/07/2008 - 20:39:10
    */
   if (HasPtr)
   {
      if (!ISPOINTER(1)) goto FIM;      
      pClientConn = (PConnection) hb_parptr(1);
      iParam = 2;
   } else {
      pClientConn = wxGetClientConnection();
      iParam = 1;
   }
      
   HB_TRACE( HB_TR_DEBUG, ("WXQQOutAux(%p)", pClientConn ));
   
   // H� conex�o ativa?
   if (!pClientConn)
      goto FIM;
      
   /*
    * � para enviar um CRLF para o cliente? Adicionado este recurso de volta pq
    * ele pode querer mandar um texto formatado com <PRE> para o cliente!
    * 03/07/2008 - 18:48:58
    */      
   if (lSendCRLF)
   {
      char sCRLF[] = {13,10,0};
      if (!wxConnection_SendText( pClientConn, sCRLF, 2L ))
         goto FIM;
   }
      
   for( iParam = iParam, iPCount = hb_pcount(); iParam <= iPCount; ++iParam )
   {
      if (!wxWebConOut( pClientConn, iParam ))
         goto FIM;

      // TODO: testar se a funcao abaixo deu algum erro!
      if (iParam < iPCount)
         if (!wxConnection_SendText( pClientConn, " ", 1L ))
            goto FIM;
   }
   
   Result = WX_SUCCESS;
   
   FIM:
      hb_retni( Result );
      return;
}

/**
 * wxQQout( <cList,...> ) -> nResult
 *
 * Envia uma lista de valores para a saida padr�o da conex�o ativa. Esta fun��o
 * possui comportamento semelhante � fun��o QQout() do Clipper. Retorna WX_RESULT
 * indicando �xito ou n�o durante este processo.
 *
 * @<cList,...>   Uma lista de express�es separadas por v�rgula � serem enviadas
 *                para a saida padr�o desta conex�o.
 *
 * 03/07/2008 - 20:39:10
 *
 * @see @request(CONIO_FUNC_LIST)
 */
HB_FUNC( WXQQOUT )
{
   WXQQOutAux( FALSE, FALSE );
}

/**
 * wxQout( <cList,...> ) -> nResult
 *
 * Envia uma lista de valores seguido de uma quebra de linha para a saida padr�o
 * da conex�o ativa. Esta fun��o possui comportamento semelhante � fun��o Qout()
 * do Clipper e caso <cList> seja omitido uma linha em branco ser� enviada. Retorna
 * WX_RESULT indicando �xito ou n�o durante este processo.
 * 03/07/2008 - 20:39:10
 *
 * @<cList,...>   Uma lista de express�es separadas por v�rgula � serem enviadas
 *                para a saida padr�o desta conex�o.
 *
 * @see @request(CONIO_FUNC_LIST)
 */
HB_FUNC( WXQOUT )
{
   WXQQOutAux( FALSE, TRUE );
}              

/**
 * wxQQoutDirect( <pConn>, <cList,...> ) -> nResult
 *
 * Envia uma lista de valores para a saida padr�o da conex�o fornecida como
 * argumento. Esta fun��o possui comportamento semelhante � fun��o wxQQout() mas
 * difere no sentido de que a conex�o desejada para destino dos dados � passada
 * no primeiro argumento, o que em ambiente multi-thread acelera e muito o envio
 * dos dados. Retorna WX_RESULT indicando �xito ou n�o durante este processo.
 * 03/07/2008 - 20:39:10
 *
 * @<pConn>    O pointer para a conex�o de destino para envio dos dados.
 *
 * @<cList,...>   Uma lista de express�es separadas por v�rgula � serem enviadas
 *                para a saida padr�o desta conex�o.
 *
 * @see @request(CONIO_FUNC_LIST)
 */
HB_FUNC( WXQQOUTDIRECT ) /* writes a list of values to the client */
{
   WXQQOutAux( TRUE, FALSE );
}

/**
 * wxQoutDirect( <pConn>, <cList,...> ) -> nResult
 *
 * Envia uma lista de valores para a saida padr�o da conex�o fornecida como
 * argumento. Esta fun��o possui comportamento semelhante � fun��o wxQout() mas
 * difere no sentido de que a conex�o desejada para destino dos dados � passada
 * no primeiro argumento, o que em ambiente multi-thread acelera e muito o envio
 * dos dados. Retorna WX_RESULT indicando �xito ou n�o durante este processo.
 * 03/07/2008 - 20:39:10
 *
 * @<pConn>    O pointer para a conex�o de destino para envio dos dados.
 *
 * @<cList,...>   Uma lista de express�es separadas por v�rgula � serem enviadas
 *                para a saida padr�o desta conex�o. Se omitido uma linha em
 *                branco ser� enviada.
 *
 * @see @request(CONIO_FUNC_LIST)
 */
HB_FUNC( WXQOUTDIRECT )
{
   WXQQOutAux( TRUE, TRUE );
}              

/**
 * wxSendHeaderDirect( <pConn>, <cHeader> ) -> NIL
 *
 * Envia um HEADER de baixo nivel ao navegador do usuario usando diretamente o
 * ponteiro de conex�o passado como primeiro argumento. Devido � esta caracter�stica
 * esta fun��o tende a ser mais r�pida em ambiente multi-threads pois evita qualquer
 * tipo de busca tentando localizar a conex�o ativa na pilha de conex�es ativa.
 * 22/11/2006 11:48:17
 *
 * @<pConn>    O pointer para a conex�o de destino para envio dos dados.
 *
 * @<cHeader>  O cabe�alho HTTP � ser enviado para a conex�o.
 *
 * @see wxSendHeaderDirect() wxSendHeader() wxQout() wxQQout()
 */
HB_FUNC( WXSENDHEADERDIRECT )
{
   PConnection pClientConn;

   WX_RES Result = WX_FAILURE;
   // Passou os parametros corretos?
   if (!ISPOINTER(1)) goto FIM;
   if (!ISCHAR(2))    goto FIM;
   
   pClientConn = (PConnection) hb_parptr(1);

   // H� conex�o ativa?
   if (!pClientConn)  goto FIM;
   
   Result = wxConnection_SendHeader( pClientConn, hb_parcx(2), hb_parclen(2) );
   
   FIM:
      hb_retni( Result );
      return;
}
 
/**
 * wxSendHeader( <cHeader> ) -> NIL
 *
 * Esta fun��o permite enviar cabe�alhos HTTP para o navegador do cliente (do
 * usu�rio que est� acessando). � de extrema import�ncia alertar que essa fun��o
 * s� funciona antes de qualquer saida de texto (HTML ou n�o) para a p�gina, por
 * este motivo que os dados que s�o enviados s�o denomidos de �cabe�alho�, pois
 * devem estar no inicio da resposta HTTP e n�o no corpo da informa��o.
 * 22/11/2006 11:48:17
 *
 * @<cHeader>  O cabe�alho HTTP � ser enviado para a conex�o.
 *
 * <sample>
 * #include "wxweb.ch"
 *
 * // Redirecionamos o navegador
 * wxSendHeader( "Location: http://www.harbour-project.org/")
 *
 * // Devemos nos certificarde que mais nenhum c�digo ser� executado depois de
 * // redirecionar o navegador usando o comando logo baixo:
 * RETURN
 * </sample>
 *
 * @see wxSendHeaderDirect() wxSendHeader() wxQout() wxQQout()
 */
HB_FUNC( WXSENDHEADER )
{
   HB_FETCH_THREAD

   WX_RES Result = WX_FAILURE;
   
   // H� conex�o ativa?
   if (!pClientConn)
      goto FIM;
   
   if (!ISCHAR(1))
      goto FIM;

   Result = wxConnection_SendHeader( pClientConn, hb_parcx(1), hb_parclen(1) );
   
   FIM:
      hb_retni( Result );
      return;
}

/******************************************************************************/
/*
 * Enviamos a saida do output para uma fun��o ESPEC�FICA do xHB
 * 30/06/2008 - 11:24:33
 */
static 
int wxConnectionCallerOutputFunc( WX_PTR pData, BYTE *Source, ULONG Length )
{
   PHB_DYNS Symbol;
   PConnection pConn;
   int Result = WX_FAILURE;

   HB_TRACE( HB_TR_DEBUG, ("wxConnectionCallerOutputFunc(%p,'%s',%lu)", pData, Source, Length ));
   
   if (!pData)
      return Result;
      
   pConn = (PConnection) pData;
      
// Por acaso nao tem o nome da funcao de captura? Ignoramos entao pq est� errado
   if (!pConn->OutPutFunc)
      return Result;
         
 //Se for a 1� vez, temos que pegar o ponteiro do SYMBOL para execu��o
   if (!pConn->OutPutSymbol)
   {
      Symbol = hb_dynsymFindName( pConn->OutPutFunc );
      pConn->OutPutSymbol = (WX_PTR) Symbol;
   } else  
      Symbol = (PHB_DYNS) pConn->OutPutSymbol;
   
   /*
    * Se ele nao achar a fun��o, iremos ignorar e n�o mandaremoso OUTPUT pra nada
    */
   if (!Symbol)
   {
      //hb_errRT_BASE_SubstR( EG_ARG, 1234, NULL, "wxWebRegisterOutputFunc - Symbol not found: %s", 1, pConn->OutPutFunc );
      HB_TRACE( HB_TR_DEBUG, ("     >>> Symbol not found: %s", pConn->OutPutFunc ));
      return Result;
   }
      
   hb_vmPushSymbol( Symbol->pSymbol );
   hb_vmPushNil();
   hb_vmPushInteger( pConn->Handle );              // 1. Handle da conexao ativa
   hb_vmPushString( (char *) Source, Length );     // 2. Texto a ser gravado                                   
   hb_vmPushLong( (long) Length );                 // 3. Tamanho da string
   hb_vmDo( 3 );    
   
   Result = hb_itemGetNI( hb_stackReturnItem() );
   return Result;
}
 
/*
 * Permite capturar a saida do pConn de tela.
 * 30/06/2008 - 11:25:19
 */
WX_RES wxWebRegisterOutputFunc( PConnection pClientConn, char *FuncName )
{
   HB_TRACE( HB_TR_DEBUG, ("wxWebRegisterOutputFunc(%p,'%s')", pClientConn, FuncName ));
   
   if (!pClientConn)
      return WX_FAILURE;
      
   if (pClientConn->OutPutFunc)
      hb_xfree(pClientConn->OutPutFunc);
      
   // Se eles mandaram uma STRING nula, limpamos o valor atual de captura
   if (FuncName)     
   {
      pClientConn->OutPutFunc  = xStrUpperNew( FuncName, strlen(FuncName) );
      pClientConn->pOutPut     = (TOutPutFunc) wxConnectionCallerOutputFunc;
   } else {
      pClientConn->OutPutFunc  = NULL;
      pClientConn->pOutPut     = NULL;      
   }
   
   /*
    * Vamos usar isto aqui para guardar o ponteiro do SYMBOL referente � fun��o
    * que ir� processar o OutPut.
    */    
   pClientConn->OutPutSymbol = NULL;
   return WX_SUCCESS;
}
 
/**
 * wxWebRegisterOutputFunc( [ <pConn>, ] <cOutPutFunc> ) -> cOldFuncName
 *
 * Registra o nome da fun��o que ir� processar as informa��es enviadas para a
 * saida padr�o na conex�o passada como argumento. Se <pConn> for omitido a conex�o
 * ativa baseada na thread atual ser� utilizada. Se <cOutPutFunc> for omitido o
 * nome da fun��o atualmente em vigor ser� retornado.
 *
 * 30/06/2008 - 11:25:19
 */
HB_FUNC( WXWEBREGISTEROUTPUTFUNC )
{   
   PConnection pClientConn;
   int i;
      
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   /* Aqui iremos retornar o nome da fun��o atual que capturar� o pConn */    
   if (pClientConn && pClientConn->OutPutFunc) 
      hb_retc( pClientConn->OutPutFunc );
   else
      hb_retc( "" );
     
   // N�o passou nenhum parametro? Pula fora!!!
   if (!ISCHAR(i))
      return; 
      
   wxWebRegisterOutputFunc( pClientConn, hb_parcx(i) );
}

/*
 * Esta fun��o ajusta o valor de uma chave j� existente ou cadastra uma nova.
 * 16/07/2008 - 09:51:51
 */ 
WX_RES wxServerAdd( PConnection pClientConn, int Pos, BYTE *Value, ULONG Length, char *KeyName )
{
   PRepositoryItem pItem;
   HB_TRACE( HB_TR_DEBUG, ("wxServerAdd( %p, %d, '%s', %lu, '%s' )", pClientConn, Pos, Value, Length, KeyName ));
   
   if (!((Pos >= 0) && 
         (Pos < pClientConn->ServerCount )))
      return WX_FAILURE;
   /*
    * Se esta posi��o na mem�ria ainda n�o houver sido preenchida, ajustamos 
    * isto agora!
    * 16/07/2008 - 10:10:39
    */  
   if (!pClientConn->pServer[ Pos ])   
      pClientConn->pServer[ Pos ] = RepositoryList_Create();

   pItem = pClientConn->pServer[ Pos ];
   
   if (KeyName)
   {
      if (pItem->Key)
         hb_xfree( (WX_PTR) pItem->Key );
         
      pItem->Key = xStrDup( KeyName );
   }
               
   if (Length == CALC_LEN)
   {
      if (Value)
        Length = strlen( Value );
      else
        Length = 0L;
   }

   //HB_TRACE( HB_TR_DEBUG, ("RepositoryList_UpdateStr(%p, %lu) ", pItem, Length ));
   return RepositoryList_UpdateStr( pItem, Value, Length );
}
 
/**
 * wxServerName( [<pConn> ,] <nIndex> ) -> cKey
 *
 * Retorna a string que identifica o nome da variavel de ambiente localizada na
 * posi��o especificada no primeiro argumento dentro da pilha de valores de
 * wxServer(). Se <pConn> for omitido a conex�o ativa baseada na thread atual
 * ser� utilizada.
 * 17/07/2008 - 19:50:12
 *
 * @see WXSERVER() WXSERVERNAME() WXGETENV() wxServerAdd() wxServerCount()
 */
HB_FUNC( WXSERVERNAME )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   int i,Pos;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxServerName( %p, %d )", pClientConn, i ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->pServer      ==>( %p )", pClientConn->pServer ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->ServerCount  ==>( %lu )", pClientConn->ServerCount ));                                                        
   hb_retc( "" );
   
   /* Se ele n�o quer achar um item nem pelo numero, nem pelo nome -- ignore! */
   if (!ISNUM(i))
   {
      HB_TRACE( HB_TR_DEBUG, ("  Parametro %d nao � NUMERICO", i));
      return;
   }
   if (!pClientConn)
      return;
   if (!pClientConn->pServer)
      return;

   Pos = hb_parni(i);
   
   if (!((Pos >= 0) && 
         (Pos < pClientConn->ServerCount )))
      return;
   
   pItem = pClientConn->pServer[ Pos ];
   hb_retc( (char *) pItem->Key );
}

/**
 * wxServer( [<pConn> ,] <nIndex | cKeyName> [, <cValue> [, <cKeyName> ]] ) -> cValue
 *
 * Manipula as vari�veis de ambiente espec�ficas de uma determinada conex�o.
 *
 * @<pConn>         O ponteiro para a conex�o que ser� manipulado. Se omitido a
 *                  conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<nIndex|cKeyName>   � um valor num�rico (indicando uma posi��o espec�fica) da
 *                      variavel de ambiente que se deseja manipular. Se o objetivo
 *                      for apenas consultar ou alterar o conte�do de uma vari�vel
 *                      j� existente, � poss�vel fornecer neste par�metro o nome (ou
 *                      ALIAS) da vari�vel desejada. Caso o programador deseje
 *                      registrar uma nova vari�vel de ambiente utilizando wxServer()
 *                      ser� obrigat�rio informar um valor num�rico neste par�metro,
 *                      representando assim o n�mero do elemento que desejamos alterar
 *                      dentro da pilha de vari�veis dispon�veis.
 *
 *                      Nota: � importante lembrar-se que internamente a wxServer()
 *                      manipula estas vari�veis de ambiente utilizando uma lista
 *                      ligada e devido � isto existe a op��o de se cadastrar um
 *                      "alias" para esta posi��o na mem�ria utilizando o quarto
 *                      par�metro <cKeyName> visando facilitar futuras refer�ncias
 *                      � esta vari�vel.
 *
 * @<cValue>        � o valor caracter que ser� atribuido � variavel de ambiente.
 *
 * @<cKeyName>      Caso estejamos registrando uma nova vari�vel de ambiente, este
 *                  argumento representa o "alias" que ser� atribuido � esta variavel.
 *
 * <sample>
 * #include "wxweb.ch"
 * FUNCTION main()
 *
 *    ? wxServer( QUERY_STRING, "#############" )
 *    ? wxServer( SERVER_PROTOCOL, "CGI/1.1", "SERVER_PROTOCOL" )
 *
 * // String no primeiro argumento � apenas para CONSULTA e ALTERACAO. Nao para cadastro!
 *    ? wxServer( "SERVER_PROTOCOL", "CGI/1.0" )
 * </sample>
 *
 * <sample>
 * #include "wxweb.ch"
 * FUNCTION main()
 *
 *  * 1� Passo: Criamos a conex�o desejada e colocamos um HANDLE opcional nela
 *    nHandle := wxConnection_Create( 1 )
 *  * 2� Passo: Ajustamos algumas variaveis internas da wxWeb
 *    wxServerCount( nHandle, MAX_SERVER_VARS )
 *    wxServer( nHandle, REQUEST_METHOD, 'POST', "REQUEST_METHOD" )
 *    wxServer( nHandle, REMOTE_ADDR   , '127.0.0.1', "REMOTE_ADDR" )
 *
 *  * Simulamos o preenchimento de campos via GET na primeira linha logo abaixo e
 *  * os via POST na segunda.
 *    wxServer( nHandle, QUERY_STRING  , 'nome=Jos%E9+Garcia+dos+Santos&id=1', "QUERY_STRING" )
 *    wxServer( nHandle, HTTP_CONTENT_BUFFER, 'end=Rua+Maria+Rosa+Greggo+Rogato%2C+n%BA+40&cep=04932240', "HTTP_CONTENT_BUFFER" )
 *
 *    wxConnection_Prepare( nHandle)
 *
 *    ...
 *
 *    wxConnection_Destroy( nHandle )
 *    RETURN nil
 * </sample>
 *
 * @see WXSERVER() WXSERVERNAME() WXGETENV() wxServerAdd() wxServerCount()
 * 08/07/2008 - 10:23:23
 */
HB_FUNC( WXSERVER )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   int i,Pos;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxServer() into %p", pClientConn ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->pServer      ==>( %p )", pClientConn->pServer ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->ServerCount  ==>( %lu )", pClientConn->ServerCount ));                                                        
   hb_retc( "" );
   
   /* Se ele n�o quer achar um item nem pelo numero, nem pelo nome -- ignore! */
   if ((!ISNUM(i)) && 
       (!ISCHAR(i)))
   {
      HB_TRACE( HB_TR_DEBUG, ("  Parametro %d nao � nem STRING, nem NUMERICO", i));
      return;
   }
   if (!pClientConn)
      return;
   if (!pClientConn->pServer)
      return;
      
   // Localizamos o item pelo nome...
   if (ISCHAR(i))
   {
      char *Name = hb_parcx(i);
      PRepositoryItem Temp;
      pItem = NULL;
      HB_TRACE( HB_TR_DEBUG, ("  Parametro %d � STRING", i));
      
      for (Pos=0; Pos < pClientConn->ServerCount; Pos++)
      {         
         Temp = pClientConn->pServer[ Pos ];

         if (!Temp)
            continue;                     
         if (!Temp->Key)
            continue;            
         if (!SameText( Temp->Key, Name))
            continue;
         
         pItem = Temp;
         break;
      }      
      
      // N�o existe nada com este nome para adicionar!!!
      if (!pItem)
         return;
         
   } else {
      HB_TRACE( HB_TR_DEBUG, ("  Parametro %d � NUMERICO", i));
      Pos = hb_parni(i);
      
      if (!((Pos >= 0) && 
            (Pos < pClientConn->ServerCount )))
         return;
      
      pItem = pClientConn->pServer[ Pos ];
   }
   
   /*
    * Aqui nos proximos 2 IFs, iremos obter o valor atual da variavel e guard�-lo
    * para retornar � fun��o/procedimento em xHB...
    */
   HB_TRACE( HB_TR_DEBUG, ("  pItem -> %p", pItem ));
   
   // Se ele armazenou um valor STRING, retornamos uma STRING   
   if (( pItem) && (pItem->Type == rtiValue))
   {
      HB_TRACE( HB_TR_DEBUG, ("  pItem->Key   -> (%s)  // Pos -> %d  ###", (char *)pItem->Key, Pos ));
      HB_TRACE( HB_TR_DEBUG, ("  pItem->Value -> (%s)", (char *)pItem->Value ));
      
      // Validamos se h� algo para retornar... 26/07/2008 - 14:57:53
      if (pItem->Value)      
         hb_retclen( (char *)pItem->Value, pItem->Len );
      else
         hb_retc("");
   }
   
   // Se ele armazenou um ITEM.. retornamos uma c�pia para este item e pulamos fora
   if (( pItem) && (pItem->Type == rtiItem))
   {                                                                
      HB_TRACE( HB_TR_DEBUG, ("  pItem->Key   -> (%s)  // Pos -> %d @@@", (char *)pItem->Key, Pos ));
      HB_TRACE( HB_TR_DEBUG, ("  pItem->Value -> (%p)", (WX_PTR) pItem->Value ));   

      // Validamos se h� algo para retornar... 26/07/2008 - 14:57:53
      if (pItem->Value)      
         hb_itemReturn( (PHB_ITEM) pItem->Value );
      else
         hb_retc("");
   } 
   
   /*
    * Aqui testamos se ele quer buscar um item ou se ele quer ALTERAR seu valor
    * 08/07/2008 - 12:09:15
    */          
   if ( ISCHAR(i+1) )
   {
      if (pItem)
         RepositoryList_UpdateStr( pItem, (BYTE *) hb_parcx(i+1), hb_parclen(i+1) );  
      else 
         wxServerAdd( pClientConn, Pos, (BYTE *) hb_parcx(i+1), hb_parclen(i+1), hb_parcx(i+2) );
   }
}

/*
 * Aloca memoria suficiente em pServer para armazenar uma qtde espec�fica de
 * itens. Retorna WX_SUCCESS em caso de sucesso!
 * 08/07/2008 - 11:33:30
 */
WX_RES wxServerCount( PConnection pClientConn, int Count )
{  
   HB_TRACE( HB_TR_DEBUG, ("wxServerCount(%p, %d)  || pClientConn->ServerCount = %d", pClientConn, Count, pClientConn->ServerCount ));
   
   if (!pClientConn)
      return WX_FAILURE;
   // Ele quer MAIS itens do que j� existe? Esque�a! S� iremos ajustar memoria 
   // uma unica vez!
   if (pClientConn->ServerCount)
      return WX_FAILURE;
 
   // Ele nao quer alocar nada? Ignore! A memoria s� ser� limpa ao finalizar...
   if (Count < 1)   
      return WX_FAILURE;

   pClientConn->pServer     = (PRepositoryItem *) hb_xgrab( sizeof( PRepositoryItem ) * Count );
   pClientConn->ServerCount = Count;
   
   memset( pClientConn->pServer, '\0', sizeof( PRepositoryItem ) * Count );
   HB_TRACE( HB_TR_DEBUG, ("    pClientConn->pServer = %p || pClientConn->ServerCount = %d", pClientConn->pServer, pClientConn->ServerCount ));
   return WX_SUCCESS; 
} 
 
/**
 * wxServerCount( [<pConn>], [<nCount>] ) -> nSize / lSucess
 *
 * Retorna e opcionalmente altera a quantidade de vari�veis de ambiente dispon�veis
 * � wxServer() na conex�o fornecida como par�metro. Se <nCount> for omitido esta
 * fun��o retornar� a quantidade de vari�veis de ambiente dispon�veis, ou caso
 * contr�rio, retornar� um valor l�gico indicando �xito ou n�o na opera��o.
 *
 * NOTA: A wxWeb s� permite que o valor de wxServerCount() seja alterado uma �nica vez.
 *
 * @<pConn>         O ponteiro para a conex�o que ser� manipulado. Se omitido a
 *                  conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<nCount>        Quantidade de elementos que desejamos reservar em wxServer()
 *                  para esta conex�o.
 *
 * 16/07/2008 - 09:41:22
 *
 * @see WXSERVER() WXSERVERNAME() WXGETENV() wxServerAdd() wxServerCount()
 */
HB_FUNC( WXSERVERCOUNT )
{
   PConnection pClientConn;
   int   Count;
 
   /*
    * Validamos aqui se estamos recebendo o ponteiro da conex�o como 1� argumento
    * ou se devemos busca-lo na lista de conexoes j� ativas.
    * 03/07/2008 - 20:39:10
    */
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      Count = hb_parni(2);
   } else {
      pClientConn = wxGetClientConnection();
      Count = hb_parni(1);
   }   
   
   if ( wxServerCount( pClientConn, Count ) == WX_SUCCESS )
      hb_retl( TRUE );
   else
      hb_retl( FALSE );
}

/**
 * wxGetEnv( <cVar> ) -> cValue
 *
 * Fun��o compat�vel com a wxWeb nas primeiras vers�es. Atualmente � implementado
 * atrav�s da funcao wxServer() 
 * 05/10/2008 - 09:25:29
 *
 * @see WXSERVER()
 */
HB_FUNC( WXGETENV ) 
{
   HB_FUNCNAME( WXSERVER )();
} 

/*
 * Registra um novo campo recebido via GET/POST/PUT � conex�o atual                  
 * 19/07/2008 - 10:16:11
 */
WX_RES wxFieldAdd( PConnection pConn, char *FieldName, BYTE *Value, ULONG Length, BOOL Encoded )
{
   PRepositoryItem pItem;
   WX_RES Result;
   
   HB_TRACE( HB_TR_DEBUG, ("wxFieldAdd( %p, '%s', '%s', %lu )", pConn, FieldName, Value, Length ));

   /* Localizamos agora o ultimo item dispon�vel */
   pItem = RepositoryList_Create();
   
   if (FieldName)
   {
      pItem->Key = xStrUpperNew( FieldName, -1 );

      HB_TRACE( HB_TR_DEBUG, ("     pItem->Key -> '%s'", pItem->Key ));
   }
               
   if (Length == CALC_LEN)
   {
      if (Value)
         Length = strlen( Value );
      else
         Length = 0L;
   }

   /* Se ele vier codificado, tornaremos o seu valor leg�vel! */
   if (Encoded)
   {
      ULONG Size;
      char *Text = wxUrlDecode( Value, Length, &Size );

      HB_TRACE( HB_TR_DEBUG, ("  Encrypted -> '%s', %lu )", Text, Size ));      
      Result = RepositoryList_UpdateStrPtr( pItem, Text, Size );
      
   } else
      Result = RepositoryList_UpdateStr( pItem, Value, Length );
   
   if ( Result == WX_FAILURE )
   {
      RepositoryList_Destroy( pItem );
      return Result;
   }
      
   /* Se este item n�o for o primeiro, ajustamos � pilha para refletir o item correto */
   if (pConn->pFields)
      pItem->pNext = pConn->pFields;
      
   pConn->FieldCount ++;
   pConn->pFields = pItem;
   return Result;
}

/**
 * wxGetFieldCount( [<pConn>] ) -> nCount
 *
 * Retorna a quantidade de campos recebidos via GET/POST/PUT na conex�o passada
 * como argumento. Se <pConn> for omitido a conex�o ativa baseada na thread atual
 * ser� utilizada.
 *
 * <sample>
 * @include( '..\..\samples\form_fields\demo.prg' )
 * </sample>
 *
 * @see WXGETFIELD() WXGETFIELDCOUNT() WXGETFIELDNAME() wxFieldExists()
 * 19/12/2006 18:03:56
 */
HB_FUNC( WXGETFIELDCOUNT )
{
   PConnection pClientConn;

   if (ISPOINTER(1))
      pClientConn = (PConnection) hb_parptr(1);
   else 
      pClientConn = wxGetClientConnection();
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetFieldCount( %p )", pClientConn ));

   if (!pClientConn)
      hb_retni(0);
   else
      hb_retni( pClientConn->FieldCount );
}

/**
 * wxGetField( [ <pConn> , ] <cFieldName | nFieldPos> ) -> cValue
 *
 * Retorna o valor de um campo HTML recebido via GET/POST/PUT na conex�o passada
 * como argumento ou NIL se a informa��o n�o for encontrada.
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<cFieldName|nFieldPos>  Se este argumento for um valor caracter, dever� conter
 *            nome do campo cujo valor deseja-se resgatar. Se este argumento for
 *            um valor num�rico, a wxWeb retornar� o campo segundo a ordem em que
 *            ele foi processado pela biblioteca.
 *
 * <sample>
 * @include( '..\..\samples\form_fields\demo.prg' )
 * </sample>
 *
 * @see WXGETFIELD() WXGETFIELDCOUNT() WXGETFIELDNAME() wxFieldExists()
 * 9/12/2006 13:07:10
 */
HB_FUNC( WXGETFIELD )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   char *Name = NULL;
   int i,Pos,Count;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetField( %p, '%s' | %d )", pClientConn, hb_parcx(i), hb_parni(i) ));
   hb_ret();               // Por padr�o retorna NIL se o campo n�o existir - 25/11/2009 - 14:45:41

   if ((!ISNUM(i)) &&      // Pediu por uma posi��o espec�fica? 
       (!ISCHAR(i)))       // Pediu por um nome espec�fico?                                              
      return;   
   if (!pClientConn)
      return;

   Pos   = 0;
   Count = 0;
   pItem = pClientConn->pFields;
   
   if (ISCHAR(i))
   {
      /* Pegamos o nome e o convertemos para uppercase e sem espa�os */
      Name = xStrUpperNew( hb_parcx(i), hb_parclen(i) );
   } else
      Pos   = hb_parni(i);

   while (pItem)
   {
         Count ++;
         
         if (!pItem->Key)
            goto LOOP;            
            
         if (Name)
         {
            if (strcmp(pItem->Key, Name))
               goto LOOP;            
         } else {
            if ( Count != Pos )
               goto LOOP;            
         }    
         
         /* Achamos o item solicitado?? */
         if (pItem->Type == rtiItem)
            hb_itemCopy( hb_stackReturnItem(), (PHB_ITEM)pItem->Value );
         else           
            hb_retclen( (char *) pItem->Value, pItem->Len );
   
         break;
         
         LOOP:
            pItem = pItem->pNext;
            continue;
   }   
   
   if (Name)
      hb_xfree( Name );
   return;
}

/**
 * wxFieldExists( [ <pConn>, ] <cFieldName> ) -> lFound
 *
 * Retorna .T. se pelo menos um campo com o nome igual � <cFieldName> tiver sido
 * recebido via m�todo GET ou POST na conex�o passada como argumento.
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<cFieldName>  Nome do campo HTML cujo nome iremos testar a exist�ncia.
 *
 * @see WXGETFIELD() WXGETFIELDCOUNT() WXGETFIELDNAME() wxFieldExists()
 * 21/12/2006 13:23:35
 */
HB_FUNC( WXFIELDEXISTS )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   char *Name = NULL;
   int i;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxFieldExists( %p, '%s' )", pClientConn, hb_parcx(i) ));
   hb_retl(FALSE);

// Pediu pelo nome?
   if (!ISCHAR(i))
      return;   
   if (!pClientConn)
      return;

   pItem = pClientConn->pFields;      
   Name  = xStrUpperNew( hb_parcx(i), hb_parclen(i) );
   
   while (pItem)
   {
         /* Achamos o item desejado? */
         if (!strcmp( pItem->Key, Name))
         {
            hb_retl( TRUE );
            return;
         }
         pItem = pItem->pNext;
   }   
   
   hb_xfree( Name );
   return;
}

/**
 * wxGetFieldName( [ <pConn>, ] <nPos> ) -> <cFieldName>
 *
 * Retorna o nome do campo recebido via metodos GET ou POST na conex�o passada
 * como argumento ou uma string vazia se o campo n�o for encontrado.
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<nPos>    Este argumento dever� ser um valor num�rico que indica a posi��o
 *            do campo que desejamos obter. Esta 'posi��o' representa a ordem em
 *            que o campo foi processado pela biblioteca.
 *
 * 21/12/2006 13:23:35
 *
 * @see WXGETFIELD() WXGETFIELDCOUNT() WXGETFIELDNAME() wxFieldExists()
 */
HB_FUNC( WXGETFIELDNAME )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   int i,Pos;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetFieldName( %p, '%d' )", pClientConn, hb_parni(i) ));
   hb_retc("");

// Pediu pelo nome?
   if (!ISNUM(i))
      return;   
   if (!pClientConn)
      return;

   Pos   = hb_parni(i);
   pItem = pClientConn->pFields;
   
   i = 1;
   
   while (pItem)
   {
         /* Achamos o item da posi��o espec�fica? */
         if (i == Pos)
         {
            hb_retc( pItem->Key );
            return;
         }
         pItem = pItem->pNext;
         i++;
   }   
   return;
}

/**
 * wxGetCookieCount( [<pConn>] ) -> nCount
 *
 * Retorna a quantidade de cookies recebidos e dispon�veis na conex�o passada como
 * argumento.
 * 19/12/2006 18:03:56
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @see WXGETCOOKIE() WXGETCOOKIENAME() WXGETCOOKIECOUNT() WXCOOKIEEXISTS() SetCookie() DeleteCookie()
 */
HB_FUNC( WXGETCOOKIECOUNT )
{
   PConnection pClientConn;

   if (ISPOINTER(1))
      pClientConn = (PConnection) hb_parptr(1);
   else 
      pClientConn = wxGetClientConnection();
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetCookieCount( %p )", pClientConn ));

   if (!pClientConn)
      hb_retni(0);
   else
      hb_retni( pClientConn->CookieCount );
}

/**
 * wxGetCookie( [<pConn> ,] <cCookieName | nCookiePos> ) -> cValue
 * Retorna o valor de um cookie recebido na conex�o passada como argumento. Dependendo
 * dos caracteres ASCII armazenados em seu cookie, poder� ser necess�rio convert�-los
 * com o uso da fun��o wxUrlDecode() antes de utiliz�-los.
 *
 * 9/12/2006 13:07:10
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<cCookieName|nCookiePos>  Se este argumento for um valor caracter, dever� conter
 *            nome do cookie cujo valor deseja-se resgatar. Se este argumento for
 *            um valor num�rico, a wxWeb retornar� o valor do cookie segundo a ordem
 *            em que ele foi recebido e processado pela biblioteca.
 *
 * <sample>
 * @include( '..\..\samples\cookies_basic\demo.prg' )
 * </sample>
 *
 * @see WXGETCOOKIE() WXGETCOOKIENAME() WXGETCOOKIECOUNT() WXCOOKIEEXISTS() SetCookie() DeleteCookie()
 */
HB_FUNC( WXGETCOOKIE )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   char *Name = NULL;
   int i,Pos,Count;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetCookie( %p, '%s' | %d )", pClientConn, hb_parcx(i), hb_parni(i) ));
   hb_ret();               // Retorna NIL se o campo nao existir - 25/11/2009 - 14:46:01

   if ((!ISNUM(i)) &&      // Pediu por uma posi��o espec�fica? 
       (!ISCHAR(i)))       // Pediu por um nome espec�fico?                                              
      return;   
   if (!pClientConn)
      return;

   Pos   = 0;
   Count = 0;
   pItem = pClientConn->pCookies;
   
   if (ISCHAR(i))
   {
      /* Pegamos o nome e o convertemos para uppercase e sem espa�os */
      Name  = xStrUpperNew( hb_parcx(i), hb_parclen(i) );
   } else
      Pos   = hb_parni(i);

   while (pItem)
   {
         Count ++;
         
         if (!pItem->Key)
            goto LOOP;            
            
         if (Name)
         {
            if (strcmp(pItem->Key, Name))
               goto LOOP;            
         } else {
            if ( Count != Pos )
               goto LOOP;            
         }    
         
         /* Achamos o item solicitado?? */
         if (pItem->Type == rtiItem)
            hb_itemCopy( hb_stackReturnItem(), (PHB_ITEM)pItem->Value );
         else           
            hb_retclen( (char *) pItem->Value, pItem->Len );
   
         break;
         
         LOOP:
            pItem = pItem->pNext;
            continue;
   }   
   
   if (Name)
      hb_xfree( Name );
   return;
}

/**
 * wxCookieExists( [ <pConn> ,] <cCookieName> ) -> lFound
 *
 * Retorna .T. se um cookie com o nome igual � <cFieldName> existir na conex�o
 * passada como argumento.
 * 21/12/2006 13:23:35
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<cCookieName>  Nome do cookie que se deseja testar.
 *
 * @see WXGETCOOKIE() WXGETCOOKIENAME() WXGETCOOKIECOUNT() WXCOOKIEEXISTS() SetCookie() DeleteCookie()
 */
HB_FUNC( WXCOOKIEEXISTS )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   int i;
   char *Name;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxCookieExists( %p, '%s' )", pClientConn, hb_parcx(i) ));
   hb_retl(FALSE);

// Pediu pelo nome?
   if (!ISCHAR(i))
      return;   
   if (!pClientConn)
      return;

   pItem = pClientConn->pCookies;   
   Name  = xStrUpperNew( hb_parcx(i), hb_parclen(i) );
   
   while (pItem)
   {
         /* Achamos o item desejado? */
         if (!strcmp( pItem->Key, Name))
         {
            hb_retl( TRUE );
            break;
         }
         pItem = pItem->pNext;
   }   
   
   if (Name)
      hb_xfree( Name );
   return;
}

/**
 * wxGetCookieName( [<pConn> ,] <nPos> ) -> <cNome>
 * Retorna o nome do cookie existente na posi��o atual.
 * 21/12/2006 13:23:35
 *
 * @<pConn>   O pointer da conex�o � ser utilizada para consulta. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<nPos>    Este argumento dever� ser um valor num�rico que indica a posi��o
 *            do cookie que desejamos obter. Esta 'posi��o' representa a ordem em
 *            que o cookie foi recebido e processado pela biblioteca.
 *
 * @see WXGETCOOKIE() WXGETCOOKIENAME() WXGETCOOKIECOUNT() WXCOOKIEEXISTS() SetCookie() DeleteCookie()
 */
HB_FUNC( WXGETCOOKIENAME )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   int i,Pos;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetCookie( %p, '%d' )", pClientConn, hb_parni(i) ));
   hb_retc("");

// Pediu pelo nome?
   if (!ISNUM(i))
      return;   
   if (!pClientConn)
      return;

   Pos   = hb_parni(i);
   pItem = pClientConn->pCookies;
   
   i = 1;
   
   while (pItem)
   {
         /* Achamos o item da posi��o espec�fica? */
         if (i == Pos)
         {
            hb_retc( pItem->Key );
            return;
         }
         pItem = pItem->pNext;
         i++;
   }   
   return;
}

/*
 * wxGetConfig( pConn, <cFieldName>[, <cNewValue>] ) --> cNewValue // cOldValue
 * Puxa um valor carregado do arquivo de configura��o para esta conex�o.
 * 05/10/2008 - 17:46:17
 */
PRepositoryItem wxGetConfig( PConnection pClientConn, char *Name, PHB_ITEM pValue )
{
   PRepositoryItem pItem = NULL;
   PRepositoryItem pPrevious;
   int i,Pos,Count;
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetConfig C( %p, '%s', %p )", pClientConn, Name, pValue ));
   
   Pos   = 0;
   Count = 0;
   pItem = pClientConn->pConfig;   
   pPrevious = NULL;
   
   while (pItem)
   {
         Count ++;
         
         if (!pItem->Key)
            goto LOOP;            
            
         if (strcmp(pItem->Key, Name))
            goto LOOP;            
   
         break;
         
         LOOP:
            pPrevious = pItem;
            pItem = pItem->pNext;
            continue;
   }   
   
   if (pItem)
   {
HB_TRACE( HB_TR_DEBUG, ("   ACHAMOS O  ITEM --> %p", pItem ));
      /* Achamos o item solicitado?? *
      
      // Nao vamos enviar nenhum resultado daqui. Deixemos isto a cargo da funcao
      // abaixo que faz a integra��o com o .PRG ou se for em C, td oq ele precisa
      // � de um ponteiro para pItem neste momento.
      // 06/10/2008 - 10:39:44
         
      if (pItem->Type == rtiItem)
         hb_itemCopy( hb_stackReturnItem(), (PHB_ITEM)pItem->Value );
      else           
         hb_retclen( (char *) pItem->Value, pItem->Len );
      /***/
   } else {   
      HB_TRACE( HB_TR_DEBUG, ("  NAO EXISTE A CHAVE" ));
   }

   if (!pValue)    // e tb nao tem nenhum valor para gravar ...
   {                                              
      HB_TRACE( HB_TR_DEBUG, ("  ERA PRA CONSULTAR APENAS - RETORNAMOS %p", pItem ));
      return pItem;        // Entao ignoramos e retornamos NIL daqui mesmo!                     
   } 
   HB_TRACE( HB_TR_DEBUG, ("  TEM QUE ALTERAR O VALOR DO ITEM %p PARA %p", pItem, pValue ));

   /*
    * Se � um item novo temos que adicionar, senao temos que fazer um update
    */
   if (!pItem)
   {
      /*
       * Tem alguma coisa para ajustar? Senao tiver, j� pulamos fora!
       */
      if ( ((char) wxItemType( pValue )) == 'U')                        
      {                                              
HB_TRACE( HB_TR_DEBUG, ("   NAO ACHAMOS E VAMOS RETORNAR NIL"));
         return NULL;   
      } 
      /* Localizamos agora o ultimo item dispon�vel */
      pItem = RepositoryList_Create();
      pItem->Key = xStrDup( Name );
      
      /* Se este item n�o for o primeiro, ajustamos � pilha para refletir o item correto */
      if (pClientConn->pConfig)
         pItem->pNext = pClientConn->pConfig;
         
      pClientConn->pConfig = pItem;
HB_TRACE( HB_TR_DEBUG, ("   VAMOS CADASTRAR --> %p --> '%s'", pItem, hb_itemGetCPtr( pValue ) ));
   } else {
      /*
       * Tipo assim .. ele ACHOU o item que precisa ser atualizado... agora vamos
       * testar se ele quer DELETAR o valor em anexo � esta informa��o (se ele
       * passar NIL como parametro).
       */ 
      if (pValue && 
            ((char) wxItemType( pValue )) == 'U')                        
      {
         PRepositoryItem pNext = pItem->pNext;
         
         if (pPrevious)
            pPrevious->pNext = pNext;
            
         if (pClientConn->pConfig == pItem)
            pClientConn->pConfig = pNext;   

         RepositoryList_Destroy( pItem );     
HB_TRACE( HB_TR_DEBUG, ("   VAMOS DELETAR   --> %p // %p", pItem, pValue ));
         return NULL;
      }
HB_TRACE( HB_TR_DEBUG, ("   VAMOS ATUALIZAR --> %p --> '%s'", pItem, hb_itemGetCPtr( pValue) ));
   }   
   
   if ( ((char) wxItemType( pValue )) == 'C')                        
      RepositoryList_UpdateStr( pItem, hb_itemGetCPtr( pValue ), hb_itemGetCLen( pValue ) );
   else
      RepositoryList_UpdateItem( pItem, pValue );      
   return pItem;
}

/**
 * wxGetConfig( [<pConn> ,] <cFieldName>, [<cDefaultValue>], [ <cNewValue>] ) -> cOldValue/cNewValue
 *
 * Puxa um valor carregado do arquivo de configura��o para esta conex�o. Se <pConn>
 * for omitido a conex�o ativa baseada na thread atual ser� utilizada.
 * 06/10/2008 - 10:30:06
 */
HB_FUNC( WXGETCONFIG )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   char *Name;
   PHB_ITEM pValue;
   PHB_ITEM pDefault;
   int i;
   
   if (ISPOINTER(1))
   {
      pClientConn = (PConnection) hb_parptr(1);
      i = 2;
   } else {
      pClientConn = wxGetClientConnection();
      i = 1;
   }
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetConfig HB( %p, '%s' )", pClientConn, hb_parcx(i) ));
   hb_ret();

   if ((!ISCHAR(i)))       // Pediu por um nome espec�fico?                                              
      return;   
   if (!pClientConn)
      return;
      
   /* Pegamos o nome e o convertemos para uppercase e sem espa�os */
   Name  = xStrUpperNew( hb_parcx(i), hb_parclen(i) );
   /* Aqui pegamos (se houver) o valor default para esta chave */
   pDefault = hb_param(i+1, HB_IT_ANY );
   /* Aqui pegamos (se houver) o novo valor atualizado para esta chave */
   pValue  = hb_param(i+2, HB_IT_ANY );
   /* Mandamos ele atualizar o item */
   pItem = wxGetConfig( pClientConn, Name, pValue );

   if (Name)
      hb_xfree( Name );
   
   /* Se ele achou algum item, entao daqui iremos retornar seu valor! */   
   if (pItem)
   {      
      if (pItem->Type == rtiItem)
         hb_itemCopy( hb_stackReturnItem(), (PHB_ITEM)pItem->Value );
      else           
         hb_retclen( (char *) pItem->Value, pItem->Len );
   } else {
   
      if (pDefault && !ISNIL(i+1))
      {
HB_TRACE( HB_TR_DEBUG, ("   RETORNAR VALOR DEFAULT -> %p --> '%s'", pDefault, hb_itemGetCPtr( pDefault) ));
         hb_itemForwardValue( hb_stackReturnItem(), pDefault );
      }
   }
}

/*
 * Carrega as configura��es � partir de uma string passada como argumento.
 * 06/10/2008 - 11:23:55
 */
WX_RES wxLoadConfigFromBuffer( PConnection pClientConn, const char *Buffer, ULONG Length )
{
   char *Str;
   char Name[513], *Value;
   char *pos, *pos2;
   ULONG size;

   PRepositoryItem pItem = NULL;
   PHB_ITEM pValue;
   WX_RES Result = WX_FAILURE;
   
   /* Nao tem conexao valida? */
   if (!pClientConn)   
      return Result;
   /* Nao tem Buffer de texto valido? */
   if (!Buffer)   
      return Result;
   /* O buffer est� vazio? */
   if (Length<1L)   
      return Result;
   
   /* Salvamos os dados para o Looping */   
   Str = Buffer;
   pValue = hb_itemNew( NULL );
   
   while ( Str && *Str )
   {
      /* tiramos lixo do nome! */
      if ((*Str == '\n') ||
          (*Str == '\r') ||
          (*Str == '\0'))
      {
         Str++;
         continue;
      }
            
      /* Removemos os espa�os iniciais se houver */
      while (*Str == ' ' || *Str == '\t')
            Str ++;
            
      /*
       * Se nao for um caracter alphanumerico iremos ignorar esta linha 
       * 06/10/2008 - 13:57:34
       */
      if ( !isalpha(*Str) )
      {
         Str = strpbrk(Str,"\n\r\0" );
         continue;
      }
          
      /*
       * Pegamos o nome da chave
       */
      pos = strchr(Str,'='); 
    
      if (!pos)
         break;
   
      ++pos;
      size = ((pos)-(Str))-1L;
      
      if (size>512L)
      {
         /* evita estouro de memoria com chave muito extensa - 06/10/2008 - 14:29:57 */
         Str = strpbrk(Str,"\n\r\0" );
         continue;
      }
      
      Name[0] = '\0';
      memcpy( Name, Str, size );         
      Name[size] = '\0';
       
      /*
       * Se no nome da chave contiver alguma quebra-de-linha iremos ignora-la, pois
       * isto pode ser algum erro no arquivo. 
       * 06/10/2008 - 13:57:34
       */
      pos2 = strpbrk(Name,"\n\r " ); 
      if ( pos2 )
      {
         size = ( pos2 ) - (Name);        // Calculamos +/- onde deve estar os caracteres invalidos         
         Str += size - 1;                 // Pulamos para esta regiao da memoria
         Str  = strpbrk(Str,"\n\r\0" );   // E � partir dae mandamos ele rodar novamente os itens
         continue;
      }
   
      /*
       * Puxamos o valor do Cookie!
       */
      pos2 = strpbrk(pos,"\n\r\0" );
      
      if (pos2)
         size = pos2-pos;            
      else 
         size = strlen( pos );

      /* Removemos os espa�os iniciais se houver */
      while (*pos == ' ' || *pos == '\t')
            pos ++;
            
      /* Removemos os espa�os finais se houver */
      while ((size>0) &&
            ((pos[size - 1L]==' ') ||
             (pos[size - 1L]=='\t')))
            size --;
                  
      /*
       * Algum path normal para se ajustar?
       * 06/10/2008 - 13:42:09
       */
      xStrUpper( Name, -1 );
      
      if ((strcmp( Name, "SESSION_PATH" ) == 0) ||
          (strcmp( Name, "DOCUMENT_PATH" ) == 0))
      {
         char *Temp;
         
         Temp = xStrNewBuff( size, pos );
         Value= wxAdJustPath( Temp, "" );
         
         if (Value)
            size = strlen( Value );
         else
            size = 0L;

         hb_xfree(Temp);
         goto LOOP;
      }  

      /*
       * � algum path de internet? Se for tem que garantir que no final haja a barra
       * normal delimitadora de PATH.
       */               
      if (strcmp( Name, "JAVASCRIPT_PATH" ) == 0)
      {
         char *Temp;
         
         Temp = xStrNewBuff( size, pos );
         Value= wxWebAdjustPath( Temp, "" );
         
         if (Value)
            size = strlen( Value );
         else
            size = 0L;

         hb_xfree(Temp);
         goto LOOP;
      }  

      Value  = xStrNDup( pos, size );      
      LOOP:

      /* Iremos adicionar este item � arvore de configuracao */
      hb_itemPutCLPtr( pValue, Value, size );
      pItem = wxGetConfig( pClientConn, Name, pValue );

      if (!pos2)
         break;

      Str = strpbrk(pos2,"\n\r\0" );
   }
     
    if (pValue)
      hb_itemRelease( pValue );
 
   return WX_SUCCESS;
}

/*
 * wxLoadConfigFromFile( <cFileName> ) --> WX_SUCCESS
 * Carrega as configura��es de um arquivo salvo em disco
 * 06/10/2008 - 11:19:34
 */
WX_RES wxLoadConfigFromFile( PConnection pClientConn, const char *FileName )
{
   char *Buffer;   
   ULONG BytesRead;
   
   // Ja evitamos dele ler o disco, caso o handle de conexao seja invalido!
   if (!pClientConn)   
      return WX_FAILURE;
      
   Buffer = xStrReadFile( FileName, &BytesRead );
   
   if (!Buffer)
      return WX_FAILURE;      
   
   return wxLoadConfigFromBuffer( pClientConn, Buffer, BytesRead );
}
 
/**
 * wxLoadConfigFromFile( [<pConn>, ] <cFileName> ) -> lSucess
 * Carrega as configura��es de um arquivo salvo em disco para uma conex�o previamente
 * criada com wxConnection_Create(). Se <pConn> for omitido a conex�o ativa
 * baseada na thread atual ser� utilizada.
 * 06/10/2008 - 10:23:58
 */
HB_FUNC( WXLOADCONFIGFROMFILE )
{
   PConnection pClientConn;   

   int p;   
   char *FileName;
   
   hb_retl( FALSE );
      
   if (ISPOINTER(1))
   {     
      pClientConn = (PConnection) hb_parptr(1);
      p = 2;
   } else {      
      pClientConn = wxGetClientConnection();
      p = 1;
   }

   /*
    * Se o primeiro parametro valido nao for uma string com o nome do arquivo,
    * pulamos fora!
    */
   if (!ISCHAR(p))
      return;

   FileName = hb_parc(1);
   
   if ( wxLoadConfigFromFile( pClientConn, FileName ) == WX_SUCCESS )
      hb_retl( TRUE );
   return;
} 

/**
 * wxRedirect( [<pConn> ,] <cUrl> [, <cParams | aParams>] ) -> nil
 *
 * Envia um cabe�alho HTTP for�ando o redirecionamento do navegador para outra p�gina.
 * 22/11/2006 - 15:27:16
 *
 * @<pConn>   O pointer da conex�o a ser utilizada para envio. Se omitido a
 *            conex�o ativa baseada na thread atual ser� utilizada.
 *
 * @<cUrl>    A URL de destino para onde desejamos redirecionar o usu�rio.
 *
 * @<cParams|aParams> Este argumento poder� ser uma string contendo um �nico par�metro
 *            � ser enviado juntamente com URL de destino via m�todo GET. A wxWeb
 *            suporta adicionalmente que seja passado um array (podendo ser bidimensional
 *            ou n�o) contendo in�meros par�metros � serem enviados.
 *
 * <sample>
 *       wxRedirect( 'www.google.com.br' )
 *
 *       wxRedirect( 'pagina2', 'nome=Renato' )
 *
 *       wxRedirect( 'pagina2', {'nome=Renato','Idade=27','Married=.t.'} )
 *
 *       wxRedirect( 'pagina2', {{'nome','Renato'},{'Idade','27'},{'Married', '.t.'}} )
 * </sample>
 */
HB_FUNC( WXREDIRECT )
{
   char URL[URL_MAX_LEN+1];
   int i,p;
   ULONG Len;
   
   PConnection pClientConn;   
           
   HB_TRACE( HB_TR_DEBUG, ("wxRedirect()" ));
   hb_retni( WX_FAILURE);
         
   URL[0] = '\0';
   Len    = 0;
   
   /*
    * Validamos aqui se estamos recebendo o ponteiro da conex�o como 1� argumento
    * ou se devemos busca-lo na lista de conexoes j� ativas.
    * 03/07/2008 - 20:39:10
    */
   if (ISPOINTER(1))
   {     
      pClientConn = (PConnection) hb_parptr(1);
      p = 2;
   } else {
      
      pClientConn = wxGetClientConnection();
      p = 1;
   }

   // Se o primeiro parametro valido nao for uma string com a URL final, ignore!
   if (!ISCHAR(p))
      return;
      
   xStrAdd( URL, &Len, "Location: ", hb_parcx(p), NULL );
   p++;
   
   /*
    * Pode-se agora  passar um array com  os  parametros a serem enviados via 
    * QUERYSTRING na URL que ser� enviada. Sendo q pode ser um ARRAY ou STRING
    * j� formatada... Neste IF abaixo, tratamos se ele � uma STRING! =D
    * 28/12/2006 08:35:38
    */
   if (ISCHAR(p))
   {
      xStrAdd( URL, &Len, "?", hb_parcx(p), NULL );
      goto FIM;
   }
   
   if (ISARRAY(p))
   {
      PHB_ITEM pArray;
      HB_ITEM  Item;
      int Count;

      pArray = hb_param( p, HB_IT_ARRAY );
      Count  = (int) pArray->item.asArray.value->ulLen;

      for(i=0;i<Count;i++)
      {
         Item.type = HB_IT_NIL;
         hb_arrayGet( pArray, i+1, &Item );

         /* Passou uma string apenas com os argumentos */         
         if (HB_IS_STRING(&Item))
         {
            if (i==0)
               xStrAdd( URL, &Len, "?", (&Item)->item.asString.value, NULL );
            else
               xStrAdd( URL, &Len, "&", (&Item)->item.asString.value, NULL );
         }
         
         /* � um array multidimensional com v�rios argumentos */         
         if (HB_IS_ARRAY( &Item ))
         {
            if (Item.item.asArray.value->ulLen >= 2L)
            {
               HB_ITEM  Key, Value;
               
               Key.type = HB_IT_NIL;
               hb_arrayGet( &Item, 1, &Key );

               Value.type = HB_IT_NIL;
               hb_arrayGet( &Item, 2, &Value );
               
               if ((HB_IS_STRING(&Key)) &&
                   (!HB_IS_NIL(&Value)))
               {
                  ULONG ulLen;
                  BOOL bFreeReq;
                  char *pszString = hb_itemString( &Value, &ulLen, &bFreeReq );                  
                  char *Temp = pszString;                                    
                  char *Text;
                  
                  if (HB_IS_NUMERIC( &Value ))
                  {
                     while (*pszString == ' ')
                     {
                         pszString++;
                         ulLen --;
                     }
                  }                     
                  Text  = wxUrlEncode( pszString, ulLen, NULL );

                  if (i==0)
                     xStrAdd( URL, &Len, "?", (&Key)->item.asString.value, "=", Text, NULL );
                  else
                     xStrAdd( URL, &Len, "&", (&Key)->item.asString.value, "=", Text, NULL );
                     
                  if (Text)     hb_xfree( Text );
                  if (bFreeReq) hb_xfree( Temp );
               }
               hb_itemClear( &Key );
               hb_itemClear( &Value );
            }
         }
               
         hb_itemClear( &Item );
      }
   }

   FIM:
      i = wxConnection_SendHeader( pClientConn, URL, Len );
      hb_retni(i);
}

/**
 * wxGetDefaultParam( [ <pConn> ] ) -> cDefaultValue
 *
 * Verifica se existe algum argumento passado em linha de comando, que possa
 * ser identificado como sendo um 'par�metro default' em uma requisi��o Web. Se
 * <pConn> for omitido a conex�o ativa baseada na thread atual ser� utilizada.
 * 24/10/2008 - 19:46:48
 *
 * <sample>
 *    http://localhost/Site.exe?downloads                --> downloads
 *    http://localhost/Main.exe?pedidos&chave=999        --> pedidos
 *    http://localhost/Site.exe?download&fileid=0151     --> download
 * </sample>
 *
 */
HB_FUNC( WXGETDEFAULTPARAM )
{
   PConnection pClientConn;
   PRepositoryItem pItem;
   char *Text, *Pos;
   int i;
   
   if (ISPOINTER(1))
      pClientConn = (PConnection) hb_parptr(1);
   else
      pClientConn = wxGetClientConnection();
   
   HB_TRACE( HB_TR_DEBUG, ("wxGetDefaultPage( %p )", pClientConn ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->pServer      ==>( %p )", pClientConn->pServer ));
   HB_TRACE( HB_TR_DEBUG, ("pClientConn->ServerCount  ==>( %lu )", pClientConn->ServerCount ));                                                        
   hb_retc( "" );
   
   if (!pClientConn)
      return;
   if (!pClientConn->pServer)
      return;

   pItem = pClientConn->pServer[ QUERY_STRING ];
   
   /* Contem algum valor atrelado � querystring? */
   if (!pItem)
      return;
      
   Text  = (char *) pItem->Value;

   if (!Text)  return;  /* Nao tem nenhum texto em anexo */
   
   Pos = strchr( Text, '&' );
   if (!Pos) return;
   i   = (Pos - Text ); 
 
   if (i>0)
   {
      char *Buffer = (char *) hb_xgrab(i+1);
      Buffer[i] = '\0';
      memcpy( Buffer, Text, i );
      /*
       * Se ele achou um sinal de '=' dentro desta string, entao isto � um 
       * argumento e esta string � invalida!
       * 24/10/2008 - 20:09:38 
       */
      if (strchr( Buffer, '=' ) == NULL )
         hb_retclenAdopt( Buffer, i );
      else
         hb_xfree( Buffer );
   }   
}
