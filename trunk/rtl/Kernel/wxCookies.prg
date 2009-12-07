/*---------------------------------------------------------------------------
 *
 *  Projeto WxWeb
 *
 *  Inicio...: Maio / 2006
 *
 *  Revisado.: 7/12/2006 10:11:31
 *
 *  Por......: Vailton Renato da Silva
 *
 *  Arquivo..: wxCookies.prg
 *                            
 *  Fun��es para manipula��o de Cookies
 *
 *---------------------------------------------------------------------------*/
#define XBASE_CC

#include "simpleio.ch"
#include "common.ch"
#include "wxWebFramework.ch"

/*
 * Devemos computar esta data com a data to servidor para podermos
 * computar o valor correto do GTM pois isto influencia tb a vida do COOKIE!
 */
#define GTM_INTERVAL ( UCT2DSTDIFF() )

/** 
 * SetCookie( <cName>, [<cValue>], [<nSecsToExpires>],[<cServerPath>], [<cDomainName>],
 *            [<bSecure>], [<bHttpOnly>] ) -> lOk
 *  
 * Cria, atualiza ou apaga um cookie do navegador do usu�rio. Esta fun��o envia 
 * os dados sobre o cookie selecinoado juntamente com o resto dos cabe�alhos    
 * HTTP. Assim como os outros cabe�alhos (headers), os cookies devem ser enviados   
 * antes de qualquer sa�da que seu programa produza (isto se deve � uma restri��o   
 * do protocolo HTTP).
 *
 * O que quer dizer que voc� deve colocar chamadas a essa fun��o antes de qualquer 
 * comando ou fun��o que gere sa�da, incluindo os espa�os e linhas em branco.
 *
 * Uma vez que o cookie foi criado, a wxWeb disponibiliza fun��es para que voc� 
 * possa acessar, consultar e at� enumerar os cookies dispon�veis no navegador do
 * usu�rio.
 *
 * 22/11/2006 11:48:17
 *
 * @<cName>             O nome do cookie que ser� manipulado.
 *
 * @<cValue>            O valor do cookie. Esse valor � guardado no computador do
 *                      cliente e n�o � recomendado guardar nenhum informa��o confidencial
 *                      atrav�s deste tipo de recurso. Utilize a fun��o wxGetCookie() para
 *                      obter o valor previamente armazenado em um Cookie. Se este argumento
 *                      for omitido o Cookie ser� excluido da CPU do usu�rio.
 *
 * @<nSecsToExpires>    O tempo em segundos para o cookie expirar.
 *
 * @<cServerPath>       O PATH no servidor aonde o cookie estar� dispon�vel. Se configurado
 *                      para '/', o cookie estar� dospon�vel para todo o dom�nio. Se
 *                      configurado para o diret�rio '/test/', o cookie estar� dispon�vel
 *                      apenas dentro do diret�rio /test/ e todos os subdiret�rios como
 *                      /test/dbf do dom�nio.
 *
 * @<cDomainName>       O dom�nio para qual o dom�nio estar� dispon�vel. Para fazer com que
 *                      ele esteja dispon�vel para todos os subdom�nios de examplo.com ent�o
 *                      voc� deve configurar ele para '.exemplo.com'.
 *
 * @<bSecure>           Indica que o cookie s� podera ser transimitido sob uma conex�o segura
 *                      HTTPS do cliente. Quando configurado para .T. o cookie ser� enviado
 *                      somente se uma conex�o segura existir. O padr�o � .F.
 *
 * @<bHttpOnly>         Quando for especificado como .T. o cookie ser� acess�vel somente sob
 *                      o protocolo HTTP. Isso � importante pois significa que o cookie n�o
 *                      ser� acess�vel por linguagens de script, como JavaScript entre outros.
 *
 * @see WXGETCOOKIE() WXGETCOOKIENAME() WXGETCOOKIECOUNT() WXCOOKIEEXISTS() SetCookie() DeleteCookie()
 *
 * <sample>
 * @include( '..\..\samples\cookies_basic\demo.prg' )
 * </sample>
 * 
 * @ignore
 * Maiores info em:
 *    http://wp.netscape.com/newsref/std/cookie_spec.html
 *    http://www.faqs.org/rfcs/rfc2965
 *    http://www.ietf.org/rfc/rfc2965.txt
 *
 * To Obtain cookies:
 *    http://www.ics.uci.edu/pub/ietf/http/rfc2109.txt
 */   
FUNCTION SetCookie( cName, cValue, nSecsToExpires, cServerPath, cDomainName, bSecure, bHttpOnly )
   LOCAL Str, cExpires
   LOCAL cTime, dDate  

      DEFAULT nSecsToExpires     TO 0
      DEFAULT cServerPath        TO ''
      DEFAULT cDomainName        TO ''
      DEFAULT bSecure            TO False
      DEFAULT bHttpOnly          TO False
      
      IF nSecsToExpires <> 0         
/*         IF ( GTM_INTERVAL < 0 )
            x := 3600 * ( GTM_INTERVAL * -1 )
         End
         
         cTime := AddSecondsToTime( Time(), x, @d )
         dDate := Date() + d*/
         
         cTime := UTCTime()
         dDate := UTCDate()
         cExpires := DateToGMT( dDate, cTime, 00, nSecsToExpires )
      ELSE
         cExpires := ''
      End

      /*
       * Convertemos isto para string se j� � o for
       */
      IF ValType( cValue ) == 'C'
         cValue :=  wxUrlEncode( cValue )
       * Del    := False
         
      ELSEIF ( cValue == NIL )         
       * � para deletar o cookie!!
         cValue := ""
       * Del    := True
         nSecsToExpires := -02101914

      ELSE
         cValue :=  wxUrlEncode( /* wxEnsureString */( cValue ) )
       * Del    := False
         
      End
      
      /*
       * Checa se o nome do cookie passado � valido
       */
      IF !__ISVALIDCOOKIENAME( cName ) THEN;
         RETURN .F.
      
      /*
       * Checa se o valor a ser enviado para o cookie � valido
       */
      IF !__ISVALIDCOOKIEVALUE( cValue ) THEN;
         RETURN .F.
         
      Str := 'Set-Cookie: ' + cName + '=' + cValue
    
      IF !Empty(cDomainName ) THEN;
         Str += "; domain=" + cDomainName

      IF !Empty(cServerPath ) THEN;
         Str += "; path=" + cServerPath

      IF !Empty(cExpires ) THEN;
         Str += "; expires=" + cExpires

//      IF !Empty(cServerPath ) THEN;
//         Str += "; path=" + cServerPath

      IF bSecure THEN;
          Str += "; secure"

      IF bHttpOnly THEN;
          Str += "; httponly"
    
      /*
       * Enviamos o Header para a aplica��o
       */
      WXSENDHEADER( Str )
   RETURN .T.
   
/**
 * DeleteCookie( <cName> ) -> lOk
 *
 * Deleta um cookie armazenado no navegador do usu�rio e retorna .T. indicando o
 * sucesso no envio do comando.
 * 7/12/2006 15:17:22
 */
FUNCTION DeleteCookie( cName )
      RETURN SetCookie( cName, NIL )
