   10 REM User Port RTC (SAF3019P) Test Program
   20 REM Reads all time registers and the dongle detection value
   30 REM
   40 REM Output format: TEST:name:value
   50 REM Terminator: DONE
   60 :
   70 REM OSBYTE 150 (&96) = read Sheila register X, result in Y
   80 REM OSBYTE 151 (&97) = write Y to Sheila register X
   90 REM User VIA ORB = &60, DDRB = &62
  100 :
  110 REM --- Dongle detection test ---
  120 REM Write BCD &71 to register 7, read back
  130 PROCwrite(7, 71)
  140 V%=FNread(7)
  150 IF V%=13 THEN PRINT "TEST:DONGLE1:PASS" ELSE PRINT "TEST:DONGLE1:FAIL:";V%
  160 REM Write 0 to register 7, read back
  170 PROCwrite(7, 0)
  180 V%=FNread(7)
  190 IF V%=0 THEN PRINT "TEST:DONGLE2:PASS" ELSE PRINT "TEST:DONGLE2:FAIL:";V%
  200 :
  210 REM --- Read time registers ---
  220 MN%=FNread(6):REM Minutes
  230 HR%=FNread(4):REM Hours
  240 DY%=FNread(2):REM Day
  250 MO%=FNread(0):REM Month
  260 YR%=FNread(1):REM Year offset
  270 :
  280 PRINT "TEST:MINUTE:";MN%
  290 PRINT "TEST:HOUR:";HR%
  300 PRINT "TEST:DAY:";DY%
  310 PRINT "TEST:MONTH:";MO%
  320 PRINT "TEST:YEAR:";YR%
  330 :
  340 PRINT "DONE"
  350 END
  360 :
  370 DEF PROCwrvia(R%,V%)
  380 A%=&97:X%=R%:Y%=V%:CALL &FFF4
  390 ENDPROC
  400 :
  410 DEF FNrdvia(R%)
  420 REM OSBYTE 150 returns read value in Y
  430 REM USR returns A + X*256 + Y*65536 + P*16777216
  440 A%=&96:X%=R%:Y%=0
  450 =(USR(&FFF4) AND &FF0000) DIV &10000
  460 :
  470 REM =============================================
  480 REM CBUS Read: read a SAF3019P register
  490 REM =============================================
  500 DEF FNread(R%)
  510 LOCAL D%, I%, B%
  520 PROCwrvia(&62, &A7)
  530 PROCclkbit(1)
  540 PROCclkbit(R% AND 1)
  550 PROCclkbit((R% DIV 2) AND 1)
  560 PROCclkbit((R% DIV 4) AND 1)
  570 PROCwrvia(&62, &06)
  580 PROCwrvia(&60, 0)
  590 PROCwrvia(&60, 2)
  600 PROCwrvia(&60, 0)
  610 D%=0
  620 FOR I%=0 TO 6
  630   PROCwrvia(&60, 2)
  640   B%=FNrdvia(&60)
  650   IF (B% AND 1)=1 THEN D%=D% OR (2^I%)
  660   PROCwrvia(&60, 0)
  670 NEXT
  680 =((D% DIV 16)*10)+(D% AND 15)
  690 :
  700 REM =============================================
  710 REM CBUS Write: write a value to a SAF3019P register
  720 REM =============================================
  730 DEF PROCwrite(R%, V%)
  740 LOCAL I%, B%
  750 B%=((V% DIV 10)*16)+(V% MOD 10)
  760 PROCwrvia(&62, &A7)
  770 PROCclkbit(1)
  780 PROCclkbit(R% AND 1)
  790 PROCclkbit((R% DIV 2) AND 1)
  800 PROCclkbit((R% DIV 4) AND 1)
  810 FOR I%=0 TO 6
  820   PROCclkbit((B% DIV (2^I%)) AND 1)
  830 NEXT
  840 PROCwrvia(&60, 0)
  850 PROCwrvia(&60, 2)
  860 PROCwrvia(&60, 0)
  870 ENDPROC
  880 :
  890 DEF PROCclkbit(B%)
  900 PROCwrvia(&60, 4 OR 2 OR (B% AND 1))
  910 PROCwrvia(&60, 4 OR (B% AND 1))
  920 ENDPROC
