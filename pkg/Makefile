#
#    $Id: Makefile 34 2006-04-29 22:33:20Z ajw $
#

webjames: clean
	mkdir -p Apps/Web
	cp -r ../!WebJames Apps/Web
	perl CreatePackage webjames
	zip -r -9 -I ^.webjames-pkg/zip Apps RiscPkg Sprites.!webjames SysVars.WebJames=24Dir  -x *svn*

site: clean
	mkdir -p Apps/Web
	cp -r ../Site Apps/Web
	perl CreatePackage site
	zip -r -9 -I ^.webjames-site-pkg/zip Apps RiscPkg -x *svn*

php-webjames: clean
	mkdir -p Apps/Web
	cp -r ../!WebJames Apps/Web
	perl CreatePackage php-webjames
	zip -r -9 -I ^.webjames-php-pkg/zip Apps RiscPkg Sprites.!webjames SysVars.WebJames=24Dir SysVars.File=24Type_18A -x *svn*

php-cgi: clean
	mkdir -p Apps/Web
	cp -r ../!PHP Apps/Web
	perl CreatePackage php-cgi
	zip -r -9 -I ^.php-pkg/zip Apps RiscPkg Sprites.!php SysVars.PHP=24Dir SysVars.File=24Type_18A -x *svn*

.PHONY: clean all


clean:
	-rm -rf Apps RiscPkg/Control

cleanall: clean
	-rm -rf ../webjames-pkg.zip ../webjames-php-pkg.zip ../php-pkg.zip