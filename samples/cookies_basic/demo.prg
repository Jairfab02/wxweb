#include "wxweb.ch"

   name := "myCookie"

// Atualizamos o cookie se ele existir. Caso o cookie n�o exista esta fun��o ir�
// cri�-lo e ele estar� dispon�vel na pr�xima vez que o usuario executar este
// aplicativo novamente.
   SetCookie( name, time(), 360 )
   
   IF wxCookieExists( name )
      ? "O cookie " + name + " existe!", br()
      ? "Sua ultima visita a esta pagina foi em", ;
            wxUrlDecode( wxGetCookie( name ) )
   ELSE
      ? "O cookie " + name + " NAO existe!"
   End

   ? br()
   ? HREF( 'demo.exe', 'Clique aqui para atualizar' )
   ?