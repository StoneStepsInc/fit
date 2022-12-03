@echo off

setlocal

if EXIST src\sha256 goto :haveit

set GIT_COMMIT=5e637272c13f200872d55ff579f7e2ab6c3f252f

curl --location --output sha256.zip https://github.com/jb55/sha256.c/archive/%GIT_COMMIT%.zip

call 7z x sha256.zip
del /Q sha256.zip

move sha256.c-%GIT_COMMIT% src\sha256
cd src\sha256

move deps\rotate-bits .

rmdir /S /Q deps
del /S /Q package.json
del .gitignore .travis.yml Makefile
del test.c

goto :EOF

:haveit

echo Directory sha256 already exists
