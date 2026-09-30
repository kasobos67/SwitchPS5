set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
break AssertFailSoftImpl
commands
silent
printf "EDEN_DEBUG_ASSERT_STOP\n"
thread apply all bt 20
quit 86
end
catch signal SIGSEGV SIGABRT SIGBUS SIGILL SIGINT
commands
silent
printf "EDEN_DEBUG_SIGNAL_STOP\n"
thread apply all bt 20
quit 87
end
run
printf "EDEN_DEBUG_NORMAL_EXIT %d\n", $_exitcode
quit $_exitcode
