/*--------------------------------------------------------------------------- 
 *
 *  Projeto WxWeb
 *
 *  Inicio...: Maio / 2006
 *
 *  Revisado.: 07/06/2007 - 11:29:35
 *
 *  Por......: Vailton Renato da Silva
 *
 *  Arquivo..: wxTrace.prg
 *                            
 *  Funcoes diversas para depura��o e trace do aplicativo atual
 *
 *---------------------------------------------------------------------------*/
#include "simpleio.ch"
#include "Fileio.ch"
#include "common.ch"
#include "wxWebFramework.ch"

static nHandle  := -1
static lConsole := False

/*
 * Ativa ou fecha as rotinas de TRACELOG da WxWeb!
 * Esta fun��o recebe 2 parametros: 
 * 
 *       lEnabled    -> .T. abre o arquivo de log, .F. fecha o arquivo 
 *       cFileName   -> Nome do arquivo que ser� criado como LOG. Se omitido, ser�
 *                      criado um arquivo com base no IP do cliente.
 *       lAppend     -> .T. indica que o LOG deve ser sempre adicionado ao arquivo
 *                      caso ele exista.
 *
 *       Se nenhum argumento for passado esta fun��o retorna um valor l�gico  
 *       indicando se as rotinas de trace estao ativas ou n�o. 
 *
 * 21/06/2007 - 19:48:34
 */
function wxTraceLog( lEnabled, cFileName, lAppend )
   local cPath, Result
   local Temp

 * Resultado padr�o (para quase todos os IFs abaixo)   
   Result := (nHandle<>-1)
   
 * Ele s� quer saber se est� ativo ou n�o?
   if PCount() <1 then;
      return Result 
      
 * � realmente um parametro BOOLEANO?  
   if valtype( lEnabled ) <> 'L' then;
      return Result
      
 * Se � realmente necess�rio procedermos com alguma opera��o?
   if lEnabled
      if nHandle <> -1 then;
         return Result
   else
      if nHandle == -1
         * Nao tem LOG aberto!
      else 
         if FClose( nHandle ) then;
            nHandle := -1
      end   
      return (nHandle<>-1)
   end
 
 * Setamos um nome default com base no IP do cliente  
   default cFileName to ''
   
 * � realmente um parametro CHAR?  
   if valtype( cFileName ) <> 'C' then;
      return Result
      
   if Empty( cFileName ) then;
      cFileName := wxClientIP() + '.LOG'
      
 * J� tem definido o diretorio? Senao, pega o padrado do WEB.INI
   if (HB_OSPATHSEPARATOR() $ cFileName)
      * Ja tem a barra de diretorio ali!
   else 
      cPath     := WXGETCONFIG('log_path')                  
      cPath     := iif( Empty( cPath ), wxExePath(), AdJustPath(cPath) )    
      cFileName := cPath + cFileName
   end

 * � para abrir um novo ou adicionar o LOG a um arquivo j� existente?   
   default lAppend   to True
   
 * Tentamos abrir o arquivo de LOG e verificamos se � para adicionar ao j� existente...  
   IF File( cFileName ) .and. lAppend
   
    * Abrimos o arquivo
      nHandle := FOpen( cFileName, FO_READWRITE )

    * Vamos para o final do arquivo
      IF nHandle <> -1 THEN;  
         FSEEK(nHandle, 0, FS_END)
         
   ELSE
      nHandle := FCreate( cFileName, FC_NORMAL )
   End
        
 * Preparamos um cabe�alho no inicio do arquivo de LOG        
   Result := Padr('* Client Info ',80,'*') + CRLF 
   Result += "Started at: " + DtoC(date()) + ' - ' + Time() + CRLF
   Result += "IP: " + wxClientIP() + CRLF
   Result += "Browser: " + wxBrowserName() + ' v' + wxBrowserVersion() + ' on ' +;
                           wxOSVersion()   + CRLF
   
   Temp := wxDeviceType()
   
   IF !Empty( Temp ) THEN;
      Result += "Device: " + Temp + CRLF      
   
 * Gravamos o cabe�alho  
   wxTrace( Result )
    
   RETURN (nHandle<>-1)

/*  
 * Ativa o eco da saida do TraceLog para o console.   
 * 22/6/2007 - 11:02:17  
 */  
FUNCTION wxTraceConsole( lEnabled )  
	LOCAL Result := lConsole
   
   IF ValType( lEnabled ) == 'L' THEN;
      lConsole := lEnabled
       
	RETURN Result
     
/*
 * Joga no LOG todos os parametros passados para nosso aplicativo e for�a 
 * opcionalmente o encerramento da aplica��o.
 * 21/5/2007 12:02:31
 */
FUNCTION wxTraceParams( lQuit )
   LOCAL i,j
   
   DEFAULT lQuit TO False

   wxTrace( "*** wxTraceParams() :" )
    
   j := FORM_COUNT()
    
   IF j>0
      FOR i:= 1 TO j
          wxTrace( "    " + FORM_GETNAME(i) + ':' + FORM_DATA(i) )
      End               
   ELSE
      wxTrace( "    (None)" )
   End
   wxTrace( "" )
   
   IF lQuit THEN;
      QUIT
      
   RETURN

/*
 * Joga na LOG todas as informa��es de ambiente dispon�veis para nosso aplicativo 
 *  e for�amos opcionalmente o encerramento da aplica��o.
 * 21/5/2007 12:02:31
 */
FUNCTION wxTraceEnv( lQuit )
   
   DEFAULT lQuit TO False

   wxTrace( "*** wxTraceEnv() :" ) 
   wxTrace( "    " + xWebLogServerVars(False) )
   wxTrace( "" )

   IF lQuit THEN;
      QUIT
      
   RETURN
      
/*  
 * Joga no arquivo de LOG os valores passados como argumentos. Retorna .T. se
 * for poss�vel gravar os dados no arquivo de LOG.  
 * 22/6/2007 - 10:42:43  
 */  
FUNCTION wxTraceEx( xVar )  
   LOCAL Type := ValType( xVar )
   
 * As rotinas de LOG estao ativas?!   
   IF nHandle == -1 then;
      RETURN False

 * NIL ?   
   IF Type == 'U' 
      Type := 'C'
      xVar := "NIL"
   End

 * Bloco ?   
   IF Type == 'B' 
      Type := 'C'
      xVar := "{|| ... }"
   End

 * Array ?   
   IF Type IN 'AO' 
      Type := 'C'
      xVar := ValToPrg( xVar )
   End
      
 * Gravamos o texto no arquivo de LOG     
   FWrite( nHandle, xVar )
   FWrite( nHandle, Chr(13) + Chr(10) )  

 * Temos que jogar o texto no console tamb�m?     
   IF lConsole THEN;
      OutStd( xVar, Chr(13) + Chr(10) )      
  
	RETURN .T.
	