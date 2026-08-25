::
:: Sample file to extend the PATH environment variable,
:: set a different PROMPT and pass arguments to 'cmd.exe'.
::
@echo off
set "PROMPT=$E[33m%USERNAME%@%COMPUTERNAME% $E[32m$P$E[0m$G "
set "PATH=lib;%PATH%"
:: Check if the command exists.
where doskey > nul
if %ERRORLEVEL%==0 (
	doskey ll=DIR $*
)
cmd %*
