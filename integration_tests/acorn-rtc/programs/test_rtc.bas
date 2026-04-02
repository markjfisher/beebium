   10 REM User Port RTC (SAF3019P) Test Program
   20 REM Reads all time registers and the dongle detection value
   30 REM
   40 REM Output format: TEST:name:value
   50 REM Terminator: DONE
   60 :
   70 REM OSBYTE 150 (&96) = read Sheila register X, result in Y
   80 REM OSBYTE 151 (&97) = write Y to Sheila register X
   90 REM User VIA ORB = &60, DDRB = &62
  100 REM PB0=DATA, PB1=CLB, PB2=DLEN
  110 :
  120 REM --- Dongle detection test ---
  130 PROCwrite(7, 71)
  140 V%=FNread(7)
  150 IF V%=13 THEN PRINT "TEST:DONGLE1:PASS" ELSE PRINT "TEST:DONGLE1:FAIL:";V%
  160 PROCwrite(7, 0)
  170 V%=FNread(7)
  180 IF V%=0 THEN PRINT "TEST:DONGLE2:PASS" ELSE PRINT "TEST:DONGLE2:FAIL:";V%
  190 :
  200 REM --- Read time registers ---
  210 MN%=FNread(6):REM Minutes
  220 HR%=FNread(4):REM Hours
  230 DY%=FNread(2):REM Day
  240 MO%=FNread(0):REM Month
  250 YR%=FNread(1):REM Year offset
  260 :
  270 PRINT "TEST:MINUTE:";MN%
  280 PRINT "TEST:HOUR:";HR%
  290 PRINT "TEST:DAY:";DY%
  300 PRINT "TEST:MONTH:";MO%
  310 PRINT "TEST:YEAR:";YR%
  320 :
  330 PRINT "DONE"
  340 END
  350 :
  360 DEF PROCwrvia(R%,V%)
  370 A%=&97:X%=R%:Y%=V%:CALL &FFF4
  380 ENDPROC
  390 :
  400 DEF FNrdvia(R%)
  410 A%=&96:X%=R%:Y%=0
  420 =(USR(&FFF4) AND &FF0000) DIV &10000
  430 :
  440 REM =============================================
  450 REM CBUS Read (matches v1.26 DNG00/TBREAD protocol)
  460 REM =============================================
  470 DEF FNread(R%)
  480 LOCAL D%, I%, B%, S%
  490 REM DNG55: Set DDRB=&07 (PB0,1,2 outputs for address phase)
  500 PROCwrvia(&62, &07)
  510 REM DNG05: Send 4-bit address (register << 1, LSB first)
  520 PROCclkbit(0):REM Start bit (bit 0 of reg<<1 = 0)
  530 PROCclkbit(R% AND 1):REM S
  540 PROCclkbit((R% DIV 2) AND 1):REM A0
  550 PROCclkbit((R% DIV 4) AND 1):REM A1
  560 REM DNG55: Set DDRB=&06 (PB0 input for reading DATA)
  570 PROCwrvia(&62, &06)
  580 REM DNG10: Load pulse (&00, &02, &00)
  590 PROCwrvia(&60, &00)
  600 PROCwrvia(&60, &02)
  610 PROCwrvia(&60, &00)
  620 REM DNG45: Write &04 (DLEN high, CLB low)
  630 PROCwrvia(&60, &04)
  640 REM DNG50: Read first bit (LA) - data available before first CLB rise
  650 S%=0
  660 B%=FNrdvia(&60)
  670 S%=(S% DIV 2) OR ((B% AND 1)*128)
  680 REM Loop 6x: CLB high, read, CLB low
  690 FOR I%=1 TO 6
  700   PROCwrvia(&60, &06):REM CLB high (rising edge shifts next bit)
  710   B%=FNrdvia(&60):REM Read DATA
  720   S%=(S% DIV 2) OR ((B% AND 1)*128)
  730   PROCwrvia(&60, &04):REM CLB low
  740 NEXT
  750 REM DNG15: Reset ORB
  760 PROCwrvia(&60, &00)
  770 REM LSR: shift right once to right-justify the 7-bit value
  780 D%=S% DIV 2
  790 REM Convert BCD to binary
  800 =((D% DIV 16)*10)+(D% AND 15)
  810 :
  820 REM =============================================
  830 REM CBUS Write (matches v1.26 TBSET protocol)
  840 REM =============================================
  850 DEF PROCwrite(R%, V%)
  860 LOCAL I%, B%
  870 REM Convert binary to BCD
  880 B%=((V% DIV 10)*16)+(V% MOD 10)
  890 REM DNG55: Set DDRB=&07
  900 PROCwrvia(&62, &07)
  910 REM DNG05: Send 4-bit address
  920 PROCclkbit(0):REM Start bit
  930 PROCclkbit(R% AND 1):REM S
  940 PROCclkbit((R% DIV 2) AND 1):REM A0
  950 PROCclkbit((R% DIV 4) AND 1):REM A1
  960 REM DNG30: Send 7 data bits LSB first
  970 FOR I%=0 TO 6
  980   PROCclkbit((B% DIV (2^I%)) AND 1)
  990 NEXT
 1000 REM DNG10: Load pulse
 1010 PROCwrvia(&60, &00)
 1020 PROCwrvia(&60, &02)
 1030 PROCwrvia(&60, &00)
 1040 REM DNG15: Reset ORB
 1050 PROCwrvia(&60, &00)
 1060 ENDPROC
 1070 :
 1080 REM =============================================
 1090 REM DNG30: Clock one bit (DLEN high, CLB high then low)
 1100 REM =============================================
 1110 DEF PROCclkbit(B%)
 1120 PROCwrvia(&60, &06 OR (B% AND 1)):REM DLEN+CLB high + DATA
 1130 PROCwrvia(&60, &04 OR (B% AND 1)):REM DLEN high, CLB low (falling edge)
 1140 ENDPROC
