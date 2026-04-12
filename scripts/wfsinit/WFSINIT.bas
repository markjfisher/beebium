     5REM > WFSInitFul

    10 REM Initialiser Version 0.90 - (C) Acorn Computers plc 1985

    20:

    30 ON ERROR VDU 28,0,24,39,0,13,10:REPORT:PRINT" at line"ERL:END

    40 from%=4:to%=8

    50 MODE7:@%=5

    60 osword%=&FFF1:osbyte%=&FFF4:osfile%=&FFDD:osgbpb%=&FFD1:oscli%=&FFF7:osfind%=&FFCE

    70 X%=0:Y%=&FF:A%=&EA:IF ((USR osbyte%)AND&FF00)=0 THEN PRINT'"6502 second processor required":END

    80 pssz%=&100:drsz%=&200

    90 bufsze%=&900

   100 DIM trcb% 20,tempname% 50,t% 1,c% 5

   110 DIM tn2% 20,pass% 256

   120 DIM buf% drsz%,dir% drsz%,cli% 80,sec0% 512,dbuff% bufsze%

   130 namelength%=20:passlength%=6:free%=26:option%=30

   140 adfsdirsize%=5:PROCto

   150 lsig%=&0000FFFF:msig%=&FFFF0000

   160 tradd%=1:cmd%=5:longth%=11

   170 X%=dbuff%:Y%=dbuff%DIV256:A%=&70:CALL osword%

   180 inuse%=0:freep%=inuse%+13:objcount%=freep%+2

   190 objname%=2:objload%=objname%+10:objexec%=objload%+4:objacc%=objexec%+4:objdate%=objacc%+1

   200 objsin%=objdate%+2:objsze%=objsin%+3

   210 infof%=10

   220 SPACE%=ASC" "

   230 PRINT''':FORi%=1TO2:PRINTCHR$141+"File Server Disc Initialiser":NEXT

   240 VDU 28,0,23,39,7

   250 INPUT"Drive number: "drivenumber%

   260 PROCoscli("dir :"+STR$drivenumber%)

   270 REPEAT:REMPROCoscli("Compact 30 4C")

   280 PROCread(sec0%,512,0):REM read sector zero, one

   290 UNTILsec0%?510<=3

   300 REPEATINPUT"Disc name   :" $tempname%

   310 IF LEN$tempname%>16 THEN PRINT"Name should be less than 16 characters"':UNTIL0

   320 IF LEN$tempname% ELSE UNTIL0

   330 i%=0:REPEATi%=i%+1:UNTILi%>=LEN$tempname% OR tempname%?i%<=SPACE% OR tempname%?i%>=127

   340 IF INSTR($tempname%," ")THEN PRINT"Space is Illegal":UNTIL0

   350 UNTILi%>LEN$tempname%-1

   360 res%=FNdrive(drivenumber%):disccylinders%=res%AND&FFFF:hds%=(res%AND&FF0000)DIV&10000:sectorspcyl%=hds%*33

   370 cymapsz%=disccylinders%*2+3

   380 DIM cymap% cymapsz%+4

   390 FORi%=0TOcymapsz%+4STEP4:cymap%!i%=0:NEXT

   400 INPUT"Next drive  :"additionfact%:additionfact%=additionfact%-drivenumber%

   410 bitmapsze%=((((sectorspcyl% DIV 8)+1) DIV 256)+1)*256

   420 DIM dblock% bitmapsze%,mpblk% bitmapsze%

   430 Q%=FNget_date_User

   440 PRINT "Please wait ...."

   450:

   460PROCto:GOTO560:REM Don't make bootable

   470 PROCto:PROCoscli("s.$.!Boot 400+8500"):g%=FNopen("$.!boot",&C0)

   480 PROCfrom:h%=FNopen(":0.$.fs",&40)

   490 REPEATPROCfrom:trcb%?0=h%:trcb%!1=dbuff%:trcb%!5=bufsze%

   500 X%=trcb%:Y%=trcb%DIV256:A%=4:CALLosgbpb%:bit%=bufsze%-trcb%!5

   510 PROCto:trcb%?0=g%:trcb%!1=dbuff%:trcb%!5=bit%

   520 X%=trcb%:Y%=trcb%DIV256:A%=2:CALLosgbpb%

   530 UNTILbit%<>bufsze%:CLOSE#g%

   540 PROCfrom:CLOSE#h%:PROCto

   550:

   560 PROCread(sec0%,512,0):REM read sector zero, one

   570 fssze%=((sec0%!0)AND&FFFFFF)*256+&4000:REM space for ADFS structure

   580:

   590 discinfo% = dblock%:fourchars% = discinfo%

   600 dname% = fourchars% + 4:nocyls% = dname% + 16

   610 nosecs% = nocyls% + 2:ndscs% = nosecs% +3

   620 secpcyl% = ndscs% + 1:szobtmp% = secpcyl% + 2

   630 addfact% = szobtmp% + 1:drinc% = addfact% + 1

   640 TheSinOfroot% = drinc% + 1:date% = TheSinOfroot% + 3

   650 startcyl% = date% +2

   660 FORi%=0TO bitmapsze%-1STEP4:dblock%!i%=0:NEXT

   670 tfree%=adfsdirsize%+2:REM for the map

   680 tfree%=tfree%+fssze%DIV256

   690 IF fssze%MOD256 THEN tfree%=tfree%+1

   700 stcyl%=tfree%DIVsectorspcyl%

   710 IF tfree%MODsectorspcyl% THEN stcyl%=stcyl%+1

   720 adfsdiscsize%=stcyl%*sectorspcyl%

   730 sec1%=stcyl%*sectorspcyl%+1

   740 sec2%=sec1%+sectorspcyl%

   750 PROCinitmaps:PROCfreeinfospace

   760 sec0%!&F6=sec1%

   770 sec0%!256=adfsdiscsize%-sec0%!0

   780 sec0%!502=sec2%

   790 sec0%!252=stcyl%*sectorspcyl%:REM new disc size

   800 sec0%?255=FNchecksum(sec0%):sec0%?511=FNchecksum(sec0%+256)

   810 PROCwrite(sec0%,512,0)

   820 FOR i%=0 TO 255 STEP 4:discinfo%!i%=0:NEXT

   830 root%=FNablk(drsz%)

   840 REM now create and write out sector 2 and sector 12

   850 $fourchars%="AFS0":$dname% = $tempname%

   860 FORi%=LEN$dname% TO 16:dname%?i%=SPACE%:NEXT

   870 nocyls%!0=disccylinders%

   880 nosecs%!0=disccylinders%*sectorspcyl%

   890 ndscs%!0=1

   900 secpcyl%!0=sectorspcyl%

   910 szobtmp%?0=bitmapsze% DIV 256

   920 addfact%?0=additionfact%

   930 drinc%?0=1:REM next logical drive

   940 TheSinOfroot%!0=root%-1

   950 date%!0=Q%

   960 startcyl%!0=stcyl%

   970 PROCwrite(discinfo%,256,sec1%)

   980 PROCwrite(discinfo%,256,sec2%): REM second sector

   990 PROCto:PROCoscli("dir"):PROCoscli("Opt 4 2")

  1000 PROCsetup(root%)

  1010 PRINT'''"** Disc initialised **"

  1020 VDU 28,0,24,39,0:END

  1030:

  1040DEFPROCread(straddr%,bytes%,startsector%)

  1050 REM read data from disc

  1060 trcb%?cmd%=8

  1070 GOTO1130

  1080:

  1090DEFPROCwrite(straddr%,bytes%,startsector%)

  1100 REM write data to disc

  1110 trcb%?cmd%=&A

  1120 IF startsector%<2 THEN straddr%?&FF=FNchecksum(straddr%)

  1130 addhi%=startsector%DIV&10000

  1140 addhi%=addhi%OR(drivenumber%*32)

  1150 trcb%?(cmd%+1)=addhi%

  1160 trcb%?(cmd%+2)=startsector%DIV&100

  1170 trcb%?(cmd%+3)=startsector%

  1180 trcb%?(cmd%+4)=0

  1190 trcb%?(cmd%+5)=0

  1200 trcb%?0=0

  1210 trcb%!tradd%=straddr%

  1220 trcb%!longth%=bytes%

  1230 A%=&72:X%=trcb%:Y%=trcb%DIV256

  1240 CALL osword%

  1250 IF trcb%?0 THEN PRINT"Disc error "~?trcb%:END

  1260ENDPROC

  1270:

  1280DEFPROCsetbit(n%)

  1290 REM set a bit

  1300 LOCALbyte%,curval%,bit%:byte%=n% DIV 8

  1310 curval%=dblock%?byte%

  1320 bit%=n% MOD 8

  1330 curval%=curval% OR (2^bit%)

  1340 dblock%?byte%=curval%

  1350ENDPROC

  1360:

  1370DEFPROCresbit(n%)

  1380 REM reset a bit

  1390 LOCALbyte%,curval%,bit%:byte%=n% DIV 8

  1400 curval%=dblock%?byte%

  1410 bit%=n% MOD 8

  1420 curval%=curval% AND (NOT (2^bit%))

  1430 dblock%?byte%=curval%

  1440ENDPROC

  1450:

  1460DEFFNcheckbit(n%)

  1470 REM check state of a bit

  1480 LOCALbyte%,curval%,bit%:byte%=n% DIV 8

  1490 curval%=dblock%?byte%

  1500 bit%=n% MOD 8

  1510=curval% AND (2^bit%)

  1520:

  1530DEFPROCgetnext

  1540 REM find next cylinder with free space

  1550 LOCALi%,nd%:i%=oset%

  1560 nd%=disccylinders%

  1570 REPEATREPEATi%=i%+1

  1580 UNTIL((cymap%!((i%-1)*2+3)MOD&10000) OR (i%=nd%))

  1590 IFcymap%!((i%-1)*2+3)MOD&10000 THEN UNTIL-1:GOTO1620

  1600 nd%=oset%-1:i%=1

  1610 UNTIL0

  1620 oset%=i%:big%=cymap%!((i%-1)*2+3)MOD&10000

  1630ENDPROC

  1640:

  1650DEFFNablk(btes%)

  1660 REM allocate a number of blocks

  1670 LOCALnblks%,sin%,alblks%,curoff%,oset%,big%:nblks%=btes%DIV256

  1680 IF btes%MOD256 THEN nblks%=nblks%+1

  1690 FORi%=0TObitmapsze%STEP4:mpblk%!i%=0:NEXT

  1700 REM find cylinder with largest free space

  1710 oset%=0:big%=0

  1720 FORi%=1 TO disccylinders%

  1730 IF cymap%!((i%-1)*2+3)MOD&10000 > big% THEN oset%=i%-1:big%=cymap%!((i%-1)*2+3)MOD&10000

  1740 NEXT

  1750 PROCread(dblock%,bitmapsze%,oset%*sectorspcyl%)

  1760 psn%=-1:REPEATpsn%=psn%+1:UNTILFNcheckbit(psn%):REM find first free block

  1770 PROCresbit(psn%)

  1780 PROCwrite(dblock%,bitmapsze%,oset%*sectorspcyl%)

  1790 sin%=oset%*sectorspcyl%+psn%

  1800 PROCremove(1)

  1810 alblks%=big%-1:IF big%-1>nblks% THEN alblks%=nblks%

  1820 $mpblk%="JesMap":mpblk%?6=0:curoff%=infof%:REM identifier for Map blocks

  1830 PROCread(dblock%,bitmapsze%,oset%*sectorspcyl%)

  1840 mpblk%!curoff%=sin%+1

  1850 mpblk%!(curoff%+3)=alblks%

  1860 FORi%=1TOalblks%:PROCresbit(psn%+i%):NEXT

  1870 PROCremove(alblks%)

  1880 PROCwrite(dblock%,bitmapsze%,oset%*sectorspcyl%)

  1890 nblks%=nblks%-alblks%

  1900 IF nblks%>0 THEN PROCgetnext:GOTO1810

  1910 mpblk%?8=btes%MOD256

  1920 PROCwrite(mpblk%,256,sin%)

  1930=sin%+1

  1940:

  1950:

  1960DEFPROCremove(n%)

  1970 REM remove blocks from cylinder map

  1980 LOCALblks%:blks%=!cymap%MOD&1000000

  1990 blks%=blks%-n%

  2000 ?cymap%=blks%:cymap%?1=blks%DIV256:cymap%?2=blks%DIV&10000

  2010 blks%=cymap%!(oset%*2+3)MOD&10000

  2020 blks%=blks%-n%

  2030 cymap%?(oset%*2+3)=blks%:cymap%?(oset%*2+4)=blks%DIV256

  2040ENDPROC

  2050:

  2060DEFPROCsetup(root%)

  2070 REM setup directories and password file

  2080 LOCALm%,i%,j%,t%,passptr%,n$,name$:FORi%=0TO511STEP4:buf%!i%=0:NEXT

  2090 PROCmake_dir("$",root%,buf%)

  2100 RESTORE 3930

  2110 REPEATINPUT"Password file (Y/N) :"t$:UNTIL(INSTR("YyNn",t$)ANDLENt$=1)

  2120 IF INSTR("Yy",t$) ELSE 2330

  2130 FORi%=0TO255STEP4:i%!pass%=0:NEXT

  2140 passptr%=FNenter_name(pass%,0,"Syst",TRUE)

  2150 READ m%:IF m% ELSE 2170

  2160 FORJ%=1 TO m%:READ name$:passptr%=FNenter_name(pass%,passptr%,name$,FALSE):NEXT

  2170 dirs%=1

  2180 REPEAT

  2190 REPEATPRINT"User name "dirs%" :";:INPUT""name$:IF name$="" THEN 2320

  2200 IF LEN(name$)>10 THEN PRINT"Name too long":UNTIL0

  2210 $c%=LEFT$(name$,1):IF FNalpha($c%) ELSE PRINT"Must start with a letter":UNTIL0

  2220 IF LEN(name$)=1 THEN 2270

  2230 A%=TRUE:FOR i%=2 TO LEN(name$)

  2240 $c%=MID$(name$,i%,1)

  2250 IF FNalphanum($c%) ELSE A%=FALSE:PRINT""""$c%""" is invalid"

  2260 NEXT:UNTILA%

  2270 block1%=FNablk(drsz%)

  2280 PROCmake_dir(name$,block1%,dir%)

  2290 PROCenter_dir(name$,&30,block1%-1,0,0,buf%)

  2300 passptr%=FNenter_name(pass%,passptr%,name$,FALSE)

  2310 dirs%=dirs%+1:UNTILdirs%>13

  2320 PROCenter_dir("Passwords",&0,FNwrite_PW-1,0,0,buf%)

  2330 PROCcopyfiles(buf%)

  2340 PROCwrite(buf%,drsz%,root%)

  2350ENDPROC

  2360:

  2370DEFFNalpha(char$)

  2380 REM check if alphabetic character

  2390 IF char$ < "A" =FALSE

  2400 IF char$ > "z" =FALSE

  2410=NOT ((char$>"Z")AND(char$<"a"))

  2420:

  2430DEFFNalphanum(char$)

  2440 REM check if alphanumeric character

  2450 IF ((char$="!")OR(char$="-")) =TRUE

  2460 IF FNnumeric(char$) THEN =TRUE ELSE =FNalpha(char$)

  2470:

  2480DEF FNcompare(S1$,s2%)

  2490 LOCAL ch1%,ch2%

  2500 REM compare two strings

  2510 FORi%=0TO9

  2520 IF s2%?i%=SPACE% THEN tn2%?i%=13 ELSE tn2%?i%=s2%?i%

  2530 NEXT

  2540 tn2%?i%=13

  2550 REM returns TRUE if S1 "<" S2

  2560 index%=0:res%=1

  2570 eq%=(LENS1$=LEN$tn2%)

  2580 REPEAT

  2590 ch1%=ASC(MID$(S1$,index%,1)):IF FNalpha(CHR$ch1%) THEN ch1%=ch1%AND&DF

  2600 ch2%=ASC(MID$($tn2%,index%,1)):IF FNalpha(CHR$ch2%) THEN ch2%=ch2%AND&DF

  2610 IF ch1%<>ch2% THEN res%=0:UNTILTRUE:GOTO2670

  2620 index%=index%+1

  2630 IF index%>LENS1$ res%=TRUE

  2640 IF index%>LEN$tn2% res%=FALSE

  2650 UNTIL res%<>1

  2660 IF eq% THEN =1 ELSE =res%

  2670=ch1%<ch2%

  2680:

  2690DEF PROCmake_dir(name$,s1%,dir%)

  2700 REM create a directory

  2710 FORi%=0TOdrsz%-1STEP4:dir%!i%=0:NEXT

  2720 $(dir%+3)=name$+"          ":dir%?(3+LEN(name$))=SPACE%

  2730 dir%!freep%=&1E5

  2740 j%=0:FORi%=&11 TO &1E6 STEP objsze%

  2750 dir%!i%=j%:j%=i%:NEXT

  2760 PROCwrite(dir%,drsz%,s1%)

  2770ENDPROC

  2780:

  2790DEF PROCenter_dir(n$,acc%,sn%,la%,ea%,buff%)

  2800 LOCALpt%,lpt%,gt%,sp%

  2810 pt%=inuse%

  2820 REPEAT

  2830 lpt%=pt%

  2840 pt%=(buff%!pt%) AND lsig%

  2850 IF pt% THEN gt%=FNcompare(n$,buff%+pt%+objname%) ELSE gt%=TRUE

  2860 UNTILgt%

  2870 IF ((buff%!lpt%) AND lsig%) ELSE buff%!lpt%=(((buff%!lpt%)AND msig%)OR((buff%!freep%)AND lsig%))

  2880 sp%=((buff%!freep%)AND lsig%)

  2890 buff%!freep%=(buff%!freep% AND msig%) OR (buff%!sp% AND lsig%)

  2900 buff%?lpt%=sp%:buff%?(lpt%+1)=sp%DIV256

  2910 buff%?sp%=pt%:buff%?(sp%+1)=pt%DIV256

  2920 $(sp%+objname%+buff%)=n$+"          ":buff%?(sp%+objname%+LEN(n$))=SPACE%

  2930 buff%!(sp%+objload%)=la%

  2940 buff%!(sp%+objexec%)=ea%

  2950 buff%?(sp%+objacc%)=acc%

  2960 buff%!(sp%+objdate%)=Q%

  2970 buff%!(sp%+objsin%)=((buff%!(sp%+objsin%))AND msig%)OR sn%

  2980 buff%!objcount%=(buff%!objcount%)+1

  2990ENDPROC

  3000:

  3010DEF FNwrite_PW

  3020 REM allocate and write out Password file

  3030 LOCALs1%:s1%=FNablk(pssz%)

  3040 PROCwrite(pass%,256,s1%)

  3050=s1%

  3060:

  3070DEF FNget_date_User

  3080 REM input the date

  3090 LOCALi%,j%,k%,z%,X%,Y%,A%,z$

  3100 REPEATINPUT"Date (dd/mm/yy): "$trcb%

  3110 j%=0

  3120 FOR k%=0 TO 1:i%=j%

  3130 j%=INSTR($trcb%,"/",i%+1):NEXT

  3140 IF j%=0 THEN UNTIL0

  3150 z$=LEFT$($trcb%,i%-1)

  3160 IF LEN z$>2 THEN UNTIL0

  3170 IF LEN z$=0 THEN UNTIL0

  3180 IF FNnumeric(z$) ELSE UNTIL0

  3190 z%=EVAL z$

  3200 IF z%>31 THEN UNTIL0

  3210 z$=MID$($trcb%,i%+1,j%-i%-1)

  3220 IF LEN z$>2 THEN UNTIL0

  3230 IF LEN z$=0 THEN UNTIL0

  3240 IF FNnumeric(z$) ELSE UNTIL0

  3250 Y%=EVAL z$

  3260 IF Y%>12 THEN UNTIL0

  3270 z$=RIGHT$($trcb%,(LEN $trcb%)-j%)

  3280 IF LEN z$>2 THEN UNTIL0

  3290 IF LEN z$=0 THEN UNTIL0

  3300 IF FNnumeric(z$) ELSE UNTIL0

  3310 X%=EVAL z$: REM X%=EVAL z$

  3315 IFX%<81:X%=X%+100

  3320 UNTILY%+z%: REM UNTILX%>=85:REM structured programming ...

  3330 =(((X%-81)*4096)+(Y%*256)+z%+((X%-81)AND&F0)*2) AND &FFFF: REM =(((X%-81)*4096)+(Y%*256)+z%) AND &FFFF

  3340:

  3350DEFFNnumeric(num$)

  3360 REM check if numeric character

  3370 LOCALA%,v$:A%=TRUE:FORl%=1TOLENnum$

  3380 v$=MID$(num$,l%,1)

  3390 IF A% THEN A%=((v$>="0")AND(v$<="9"))

  3400NEXT:=A%

  3410:

  3420DEF FNchecksum(data%)

  3430 REM Forms end-around carry checksum on 255 bytes of data

  3440 LOCAL csum%,i%

  3450 csum%=&FF

  3460 FORi%=254 TO0  STEP -1

  3470 IF (csum% DIV 256)>0 THEN csum%=(csum% AND &FF)+1

  3480 csum%=csum%+data%?i%

  3490 NEXT

  3500=csum% AND &FF

  3510:

  3520DEFPROCinitmaps

  3530 REM initialise the disc maps

  3540 LOCALfreesecs%,ttsecs%:freesecs%=sectorspcyl%-1:ttsecs%=0

  3550 FOR j%=0 TO bitmapsze%-1 STEP 4:dblock%!j%=0:NEXT:FOR j%=1 TO sectorspcyl%-1:PROCsetbit(j%):NEXT

  3560 FOR i%=stcyl% TO disccylinders%-1

  3570 IF i%=0 THEN PROCread(dblock%,bitmapsze%,i%*sectorspcyl%):FOR j%=1 TO sectorspcyl%-1:PROCsetbit(j%):NEXT

  3580 IF i%=1 THEN FOR j% =0  TO bitmapsze%-1 STEP 4:dblock%!j%=0:NEXT:FOR j%=1 TO sectorspcyl%-1:PROCsetbit(j%):NEXT

  3590 PROCwrite(dblock%,bitmapsze%,i%*sectorspcyl%)

  3600 cymap%!(i%*2+3)=freesecs%

  3610 ttsecs%=ttsecs%+freesecs%

  3620 NEXT

  3630 ?cymap%=ttsecs%MOD256:cymap%?1=ttsecs%DIV256:cymap%?2=ttsecs%DIV&10000

  3640ENDPROC

  3650:

  3660DEFPROCfreeinfospace

  3670 REM free space for info sectors

  3680 LOCAL cysec1%,cysec2%

  3690 cysec1%=(sec1%DIVsectorspcyl%)*sectorspcyl%

  3700 cysec2%=(sec2%DIVsectorspcyl%)*sectorspcyl%

  3710 IF (sec1%DIVsectorspcyl%)<stcyl% GOTO3760

  3720 PROCread(dblock%,bitmapsze%,cysec1%)

  3730 PROCresbit(sec1%MODsectorspcyl%)

  3740 PROCwrite(dblock%,bitmapsze%,cysec1%)

  3750 PROCsubcyl(1,(sec1%DIVsectorspcyl%))

  3760 PROCread(dblock%,bitmapsze%,cysec2%)

  3770 PROCresbit(sec2%MODsectorspcyl%)

  3780 PROCwrite(dblock%,bitmapsze%,cysec2%)

  3790 PROCsubcyl(1,(sec2%DIVsectorspcyl%))

  3800ENDPROC

  3810:

  3820DEFPROCsubcyl(n%,c%)

  3830 REM free space in cylinder map

  3840 LOCALttsecs%,t%:ttsecs%=ttsecs%-n%

  3850 t%=cymap%!(c%*2+3)AND&FFFF

  3860 t%=t%-n%

  3870 cymap%?(c%*2+3)=t%:cymap%?(c%*2+4)=t%DIV256

  3880 ?cymap%=ttsecs%:cymap%?1=ttsecs%DIV&100:cymap%?2=ttsecs%DIV&10000

  3890ENDPROC

  3900:

  3910 REM data for creating new directories

  3920 REM data is in the form [number of entries,entry 1 string,..]

  3930 DATA 2,Boot,Welcome

  3940 REM files to be transferred in the form [network directory name, disc directory name, name1,name2,....,] ending in a null name

  3950 DATA Welcome,:0.W

  3960 DATA Alpha,Batball,Biorthm,Bpart2,Calc

  3970 DATA Clock,Help,Index,Keybd,Kingdom

  3980 DATA Message,Music,Pattern,Phone

  3990 DATA Photo,Poem,Sketch,Welcome,

  4000 DATA Library,:2.L

  4010 DATA Close,Discs,Flip,Free,Fs,Lcat

  4020 DATA Lex,Notify,Prot,Ps,RdFree,Remote,SetFree,Unprot,Users,View,

  4030 DATA Utils,:0.U

  4040 DATA Archive,L2to3,Netmgr,GetBack,Init,LogCopy,SetTime

  4050 DATA ,,

  4060:

  4070DEFPROCcopyfiles(root%)

  4080 RESTORE 3950

  4090 LOCALdirsn%,hla%,hea%,hsz%,flsn%,netdir$,discdir$

  4100 INPUT"Copy master directories (Y/N) :"netdir$:IF ((netdir$="Y")OR(netdir$="y")) THEN READ netdir$,discdir$ ELSE ENDPROC

  4110 A%=229:X%=1:Y%=0:CALLosbyte%

  4120 REPEATPRINT'"Copying "netdir$" directory .."

  4130 PROCfrom:PROCoscli("DIR "+discdir$):PROCto

  4140 dirsn%=FNablk(drsz%)

  4150 PROCmake_dir(netdir$,dirsn%,dir%)

  4160 PROCenter_dir(netdir$,&30,dirsn%-1,0,0,root%)

  4170 READ $tempname%

  4180 REPEAT:PRINT"Copying "$tempname%" .."

  4190 PROCfrom

  4200 !trcb%=tempname%

  4210 A%=5:X%=trcb%:Y%=trcb%DIV256:CALL osfile%

  4220 hla%=trcb%!2:hea%=trcb%!6:hsz%=trcb%!10

  4230 PROCto

  4240 flsn%=FNablk(hsz%)

  4250 PROCenter_dir($tempname%,&15,flsn%-1,hla%,hea%,dir%)

  4260 PROCtransfile($tempname%)

  4270 READ $tempname%

  4280 UNTIL$tempname%=""

  4290 PROCwrite(dir%,drsz%,dirsn%)

  4300 READ netdir$,discdir$

  4310 UNTILnetdir$=""

  4320 A%=229:X%=0:Y%=0:CALLosbyte%

  4330ENDPROC

  4340:

  4350DEFPROCtransfile(name$)

  4360 LOCAL h%,curoff%,cursize%,cursin%,chunk%

  4370 PROCfrom:h%=FNopen(name$,&40):IF h% ELSE PRINT"File "name$" not found":ENDPROC

  4380 curoff%=infof%:cursin%=(mpblk%!curoff%)AND&FFFFFF

  4390 IF cursin% ELSE 4540

  4400 REPEAT

  4410 cursize%=(mpblk%!(curoff%+3)AND&FFFF)*256

  4420 REPEAT

  4430 PROCfrom

  4440 chunk%=cursize%:IF chunk%>bufsze% THEN chunk%=bufsze%

  4450 ?trcb%=h%:trcb%!1=dbuff%:trcb%!5=chunk%

  4460 X%=trcb%:Y%=trcb%DIV256:A%=4:CALLosgbpb%

  4470 PROCto:PROCwrite(dbuff%,chunk%,cursin%)

  4480 cursize%=cursize%-chunk%

  4490 cursin%=cursin%+(chunk%DIV256)

  4500 UNTILcursize%=0

  4510 curoff%=curoff%+5

  4520 cursin%=(mpblk%!curoff%)AND&FFFFFF

  4530 UNTILcursin%=0

  4540 PROCfrom:CLOSE#h%:PROCto

  4550 ENDPROC

  4560:

  4570DEFFNfindsin(n$)

  4580 LOCALpt%,gt%:pt%=inuse%

  4590 REPEAT

  4600 pt%=buf%!pt% AND lsig%

  4610 gt%=pt%=0: IF gt% ELSE gt%=FNcompare(n$,buf%+pt%+objname%)=1

  4620 UNTILgt%

  4630IF pt% THEN =buf%!(pt%+objsin%)AND&FFFFFF ELSE =0

  4640:

  4650DEFPROCfrom:PROCfs(from%):ENDPROC

  4660:

  4670DEFPROCto:PROCfs(to%):ENDPROC

  4680:

  4690DEFPROCfs(no%)

  4700 REM selects filing system

  4710 LOCAL A%,X%,Y%

  4720 A%=&8F:X%=&12:Y%=no%:CALLosbyte%

  4730ENDPROC

  4740:

  4750DEFPROCoscli($cli%)

  4760 LOCAL X%,Y%,A%

  4770 X%=cli%:Y%=cli%DIV256:CALLoscli%

  4780ENDPROC

  4790:

  4800DEFFNdrive(d%)

  4810 REM get drive characteristics

  4820 LOCAL X%,Y%,A%

  4830 trcb%?0=0:trcb%!1=buf%:trcb%!5=&1A+(d%*8192):trcb%!9=22

  4840 X%=trcb%:Y%=trcb%DIV256:A%=&72:CALL&FFF1

  4850=buf%?14+(buf%?13*256)+(buf%?15*256*256)

  4860:

  4870DEFFNenter_name(pass%,passptr%,name$,priv%)

  4880 $(pass%+passptr%)=name$

  4890 pass%?(passptr%+namelength%)=13:pass%!(passptr%+free%)=&40404:pass%?(passptr%+option%)=&80+(priv%AND&40)

  4900 = passptr%+option%+1

  4910:

  4920DEFFNopen(name$,how%)

  4930 LOCAL A%,X%,Y%:DIM name%-1:$name%=name$

  4940 X%=name%:Y%=name%DIV256:A%=how%

  4950=(USRosfind%)AND&FF
