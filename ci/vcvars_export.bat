@echo off
call %1 > nul
"%2" -c "export -p"