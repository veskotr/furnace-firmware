!General Rules and Practices!
No.	General Rule or Practice
1	 All instructions over serial: are terminated with three bytes of 0xFF 0xFF 0xFF
ie: decimal: 255 or hex: 0xFF or ansichar: ÿ or binary: 11111111
ie byte ndt[3] = ; write(ndt,3); or print("\xFF\xFF\xFF"); or print("ÿÿÿ")
2	 All instructions and parameters are in ASCII
3	 All instructions are in lowercase letters
4	 Blocks of code and enclosed within braces can not be sent over serial
this means if, for, and while commands can not be used over serial
5	 A space char 0x20 is used to separate command from parameters.
6	 There are no spaces in parameters unless specifically stated
7	 Nextion uses integer math and does not have real or floating support.
8	 Assignment are non-complex evaluating fully when reaching value after operator.
9	 Comparison evaluation is non-complex, but can be joined (see && and ||).
10	 Instructions over serial are processed on receiving termination (see 1.1)
11	 Character escaping is performed using two text chars: \r creates 2 bytes 0x0D 0x0A, \" 0x22 and \\ for 0x5C
12	 Nextion does not support order of operations. sys0=3+(8*4) is invalid.
13	 16-bit 565 Colors are in decimal from 0 to 65535 (see 5.Note)
14	 Text values must be encapsulated with double quotes: ie "Hello"
15	 Items which are specific to Enhanced Models are noted with 
16	Transparent Data Mode (used by  addt and  wept commands)
MCU sending to Nextion
MCU sends command. ie: wept 30,20ÿÿÿ or addt 1,0,320ÿÿÿ
Nextion requires ~5ms to prepare for transparent mode data transfer
Nextion sends "Ready" 0xFE 0xFF 0xFF 0xFF Return Data (see 7.32)
MCU can now send specified quantity (20) of raw bytes to Nextion
Nextion receives raw bytes from MCU until specified quantity (20) is received
Nextion sends "Finished" 0xFD 0xFF 0xFF 0xFF Return Data (see 7.31)
MCU and Nextion can proceed to next command
Note: Nextion will remain waiting at step 5 until every byte of specified quantity is received.
– During this time Nextion can not execute any other commands, and may indeed hang if the MCU fails to deliver the number of bytes as specified by the command parameter.
– data quantity limited by serial buffer (all commands+terminations + data < 1024)

17	 Only component attributes in green and non readonly system variables can be assigned new values at runtime. All others are readonly at runtime with the exception of .objname
18	 Numeric values can now be entered with byte-aligned hex. ie: n0.val=0x01FF
19	 Advanced. Address mode is an advanced technique prepending the serial instruction with two bytes for the address. Two byte address is to be sent in little endian order, ie: 2556 is sent 0xFC 0x09. By default, the Nextion address is 0 and does not require two byte prefixing. When the two byte addressing is used, Nextion will only respond to the command if its address matches the two byte prefix, or the transmitted address is 65535 broadcast. Setting addr will persist and be the new power on default until set again. See 6.20 for the addr System Variable.
Address Mode allows for up to 2560 individual addresses (256 to 2815) and broadcasting to all using address prefixing of 65535. When in Address Mode, all commands sent serially must be prefixed by their two byte address or via the broadcast address. The Nextion can indeed be operated without Address Mode in its traditional manner. Using addr=0 in your project's Program.s tab will ensure the Nextion starts with Address Mode turned off.

// Nextion 3 byte termination as per NIS 1.1
uint8_t ndt[3]= ;

// Turn Address Mode on and set Address to 2556
Serial.print("addr=2556");
Serial.write(ndt,3);

// ref t0 on the Nextion with address 2556 0x09FC
// Address is sent in little endian order of lowest first
Serial.write(0xFC);
Serial.write(0x09);
Serial.print("ref t0");
Serial.write(ndt,3);

// turn off Address Mode for all and set Address to 0 using broadcast 65535
Serial.write(0xFF);
Serial.write(0xFF);
Serial.print("addr=0");
Serial.write(ndt,3);
20	 Advanced. Protocol Reparse mode is an advanced technique that allows users to define their own incoming protocol and incoming serial data handling. When in active Protocol Reparse mode, incoming serial data will not be processed natively by the Nextion firmware but will wait in the serial buffer for processing. To exit active Protocol Reparse mode, recmod must be set back to passive (ie: in Nextion logic as recmod=0), which can not be achieved via serial. Send DRAKJHSUYDGBNCJHGJKSHBDNÿÿÿ via serial to exit active mode serially. Most HMI applications will not require Protocol Reparse mode and should be skipped if not fully understood.
Protocol Reparse enables the user to assume responsibility of the incoming serial data, its processing and handling. Protocol Reparse requires users to use a combination of the Nextion Instruction Set, code assignments, the ucopy (3.33) and udelete (3.46) instructions, and System Variables recmod (6.24) and usize (6.25), the incoming serial data u[index] array (6.26) .and the exit constant DRAKJHSUYDGBNCJHGJKSHBDNÿÿÿ (1.20). The user can then use their written code within an Event (such as a Timer) and triggered (by the user design choice) for execution.

It becomes important for the user to ensure that their variables and component attributes are initialized as they require, and ensure clearing of the Serial buffer (see code_c and udelete) to ensure overflow is avoided. Coding for serial data processing and handling is indeed within the users domain and responsibility.

Program.s tab

// Set Protocol Reparse to active mode
recmod=1
Page Preinitialize Event of your page with the 4 Progress Bars

// Initialize all 4 Progress Bar's .val to 0
j0.val=0
j1.val=0
j2.val=0
j3.val=0
Timer Event to process Nextion side incoming Serial

//if the buffer contains atleast 4 bytes:
//  process 1 byte for each progress bar and
//  remove the 4 processed bytes from the Serial buffer
if(recmod==1)

MCU side code to update the 4 progress bars

uint8_t pb[4];
uint8_t bar0, bar1, bar2, bar3;

void update_progress_bars(void) 

update_progress_bars();
MCU side code to exit active Protocol Reparse

Serial.print("DRAKJHSUYDGBNCJHGJKSHBDNÿÿÿ");
The Nextion can indeed be operated without Protocol Reparse mode in its traditional manner. As power on default for recmod is 0, this ensures the traditional use of Nextion and serial command/data handling by the Nextion firmware. Setting recmod in the Program.s tab will ensure your project makes use of Protocol Reparse (or not) as you desire.

21	 Commenting user code inside Events uses the double-slash (two characters of forward slash / (ASCII byte 0x2F)) technique. See 2.30 for proper usage.
22	 Advanced. Using CRC for Instructions over Serial has been supported since v0.58. Under normal circumstances, Instructions over Serial are sent as normal (triple 0xFF termination in NIS 1.1) and CRC is not required. If you desire to activate instructions CRC in your HMI, then use the following procedures:
[[optional addr][instruction [parameters]]][optional CRC16+0x01][termination]

1) if using address mode, send the 2 byte address (see NIS 6.22 and 1.20)
2) The CRC 16 verification algorithm calculates from the first byte of the instruction to the last byte of all the instruction parameters (portion before traditional termination). To be clear, if the optional address mode is used then the two byte address IS part of the CRC16 CRC check. ie:
a) cls 31ÿÿÿ then cls 31 is the data portion for CRC16
63 6C 73 20 33 31 57 EB 01 FE FE FE
b) (addr 2016) à•cls 31ÿÿÿ then à•cls 31 is the data portion for CRC16
E0 07 63 6C 73 20 33 31 6F 73 01 FE FE FE
3) The CRC16 of the instruction+parameters requires you to send 3 bytes:
the first 2 bytes (in little endian order) are the CRC16 checksum,
and the third required byte is the constant 0x01.

static U8 InvertUint8(U8 data) 
static U16 InvertUint16(U16 data) 
U16 CRC16_MODBUS(U8* data, int lenth) 
4) Change the Nextion Termination
from the traditional triple 0xFF (NIS 1.1) to a triple 0xFE
5) If the verification of the CRC checksum fails then
the Nextion will send the Nextion Return Data 0x09 (NIS 7.8)
if verification of the CRC checksum passes then
the Nextion will send Nextion Return Data as per the bkcmd (NIS 6.13) setting.

23	 Advanced. Intelligent Series: Using ExPictures in Memory.
Two Picture formats are supported for ExPicture Components
1) *.jpg is supported IFF (if and only if) encoded as DCT baseline (or will not display)
2) *.xi is supported (*.xi files are created with the PictureBox Tool)
Using the ExPicture Component, set its .path attribute[ExPicture].path="[location][filename]"

When the location is Nextion SRAM
- ensure to reserve space via Device>Project>Memory file storage size:
- exp0.path="ram/blue.jpg"

When the location is Nextion microSD
- ensure microSD is formatted FAT32 and size <32GB
- exp0.path="sd0/blue.xi"

Display Orientation is important to observe
a) if Nextion device is in Native orientation 0° then
picture does not need to be rotated
b) if Nextion device is in an alternate orientation (not 0°) then
the picture needs to be rotated in the same orientation as the HMI

24	 Advanced. Intelligent Series: twfile file upload
Sending a Picture or any file over Serial using the twfile instruction (NIS 3.36)
twfile [location][filename],[filesize]

Step 1: issue the twfile instruction by location

When the location is Nextion SRAM
- ensure to reserve space via Device>Project>Memory file storage size:
- twfile "ram/blue.jpg",3282 // create blue.jpg with size of 3282 bytes
When the location is Nextion side microSD
- ensure microSD is inserted at runtime
- likely set Device>Project>One-time update of TFT Files to checked
- twfile "sd0/blue.jpg",3282 // create blue.jpg with size of 3282 bytes

When Nextion receives a valid twfile instruction
- it immediately tries to create the file with the size specified
IF the file creation fails: Nextion Return Data 0x06 is sent (NIS 7.7)
- Nextion will end, exiting back into default instruction handling mode
IF the file creation is successful, Nextion sends 0xFE 0xFF 0xFF 0xFF
- Nextion enters into a modified transmission protcol (goo to Step 2)

Step 2: Transparent File Data Mode

A complete packet is comprised of 2 parts: a) header and b) the data.

a) the 12 byte header consists of 4 parts: pkConst + vType + pkID + dataSize
- where pkConst is 7 bytes: 3A A1 BB 44 7F FF FE
- where vType verification is 1 byte: 0x00 no CRC, 0x01 CRC16, 0x0A CRC32
- where pkID is 2 bytes little endian: starting at 0 and increasing 1 each successive packet
- where dataSize is 2 bytes little endian: sizeof(data bytes) + sizeof(CRC bytes) 1 to 4096.
- sizeof(CRC bytes) specified by vType: 0 bytes no CRC, 2 bytes CRC16, 4 bytes CRC32

b) the packet data: file data + CRC(file data)
- send the data bytes from the file: (dataSize-sizeof(CRC bytes)) bytes worth
- followed by sending the CRC in little endian order for the data

After each packet is received:
if the packet is successful, Nextion returns 1 byte 0x05
- increment the header pkID by +1, prepare the next packet
if the packet fails, Nextion returns 1 byte 0x04
- recheck and resend the current packet using the same pkID

When all the packets are sent and successfully received
- Nextion returns 4 bytes 0xFD 0xFF 0xFF 0xFF
- Nextion will end transmission mode exiting back into default instruction handling mode.

NOTES:

The twfile instruction is indeed quite flexible: allowing each packet to vary the packet dataSize and/or CRC check used for each packet. In this manner, one can send with ease variable length text, or one can send fixed length records, or combinations of each as they see fit. The Nextion twfile does not have a repositionable file pointer, rather its internal file pointer advances to the end position of the last successful packet. The twfile will remain in its file data mode until either aborted or the specified number of data bytes have been successfully received.

If packet transmission fails (on 0x04 or no response for 500ms)
- recheck and resend the current packet using the same pkID

If many of the packets are failing
- stop more than 20ms and resend the packet again

If you want to abort the transfer midstream
- pause +20ms and send a zero byte data, no CRC packet with pkID=65535
- Nextion will stop the transfer and return 0xFD 0xFF 0xFF 0xFF
  (abort packet): 31 A1 BB 44 F7 FF FE FF FF 00 00 00

If your legit data were really to contain an abort packet
- it would be treated and recorded as data as it does not meet the 20ms delay

Note the valid picture requirements in the ExPicture entry (NIS 1.23)
Note the CRC16 entry for how to calculate packet data CRC16 value (NIS 1.22)

25	 The Program.s tab
The Program.s tab was introduced in version v1.60.0 of the Nextion Editor. This contains any of the HMI's start up run-once code before the Nextion enters into its pages. There are three sections: global integer declarations, instructions, and the page directive.
1) Global integers can be declared using
int [variable[=initialvalue]][,variable[=initialvalue]]...[,variable[=initialvalue]]
such as int sys0=0,sys1=0,sys2=0
2) Instruction section can be most Nextion instructions (no GUI Instructions, there is no page loaded)
page0.va0.txt="myConstant" //where va0 on page0 has .vscope set to global
and setting System Variables to the projects values, such as:
baud=921600
dim=100
recmod=0
addr=0
bkcmd=3
the last line of the Instruction Section is usually Nextion Preamble and Nextion Ready
printh 00 00 00 FF FF FF 88 FF FF FF // NIS 7.19 and NIS 7.29
3) The Page Directive: The page directive does not need to be page 0, but can be set to start the HMI on any page in the project: (ie: page splash, or page 4).
All declarations come before instructions, and instructions before the page directive. Once the page changes with the page instruction, it will not at anytime return to the Program.s to continue executing any statements that are written below the page instruction. All statements below page will not run ... ever.

26	 Hexadecimal constants are byte aligned, starting with 0x and are followed by an even number of hex digits. An odd number of hex digits will fail with Nextion Return Data 0x1A (see 7.11)
ie: n0.val=0x23, n0.val=0x2133, n0.val=0x123423 or n0.val=0x33223523
27	 Advanced. Nextion Upload Protocol. Full details on the nextion.tech blog here.
Step 1: Make sure you know your Nextion Serial settings (port, baudrate, mode).
You could otherwise you can scan each port at each baudrate to locate your Nextion.
DRAKJHSUYDGBNCJHGJKSHBDNÿÿÿ // abort Protocol Reparse Mode w/termination
connectÿÿÿ // issue connect instruction with termination
ÿÿconnectÿÿÿ // issue connect on Address Mode broadcast w/termination
Doing this at each of the valid baud rates (see 6.3), your Nextion should respond when
the baud rate the device is using receives the connect instruction. Between baud rate
attempts you should have a delay of (1000000/baudrate) + 30ms.

Step 2: Current Configuration.
Once you receive a valid connect string starting with comok you can proceed. If you
are using Address Mode, you will likely want to reset addr back to 0 before hand.

Step 3: Dim (6.2) and Sleep (6.12)
At this point you can optionally choose to increase the backlight and disable sleep

Step 4: Upload your *.TFT file with whmi-wri, when ready Nextion sends a 0x05 byte.
usage: whmi-wri filesize, baud,res0ÿÿÿ
where filesize in size is in bytes, devices baudrate, and res0 is any ASCII character
ie: whmi-wri 1538432,115200,Aÿÿÿ // upload file of 1,538,432 bytes at 115200 baud
Split your file into 4096 byte chunks with the final chunk as a remainder partial chunk.
After sending each 4096 bytes and final chunk wait for Nextion's 0x05 Ready byte.

Step 5: After Upload completes, allow time for Nextion to update its firmware. Avoid
performing a power cycle during Nextion's firmware update. If using the printh
Preamble and Nextion Ready in Program.s, you will know when all is completed.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------
!Operational Commands!
No.	Name	Param Count	Description and Usage/Parameters/Examples
1	page	1	 Change page to page specified. Unloads old page to load specified page.
Nextion loads page 0 by default on power on.
usage: page
is either the page index number, or pagename
page 0 // Change page to indexed page 0
page main // Change page to the page named main
2	ref	1	 Refresh component (auto-refresh when attribute changes since v0.38)
- if component is obstructed (stacking), ref brings component to top.
usage: ref
is component's .id or .objname attribute of component to refresh
- when is 0 (page component) refreshes all on the current page.
ref t0 // Refreshes the component with .objname of t0
ref 3 // Refreshes the component with .id of 3
ref 0 // Refreshes all components on the current page (same as ref 255)
3	click	2	 Trigger the specified components Touch Press/Release Event
As event code is always local, object can not be page prefixed
usage: click ,
is component's .id or .objname attribute of component to refresh
is 1 to trigger Press Event, 0 to trigger Release Events
click b0,1 // Trigger Touch Press Event of component with .objname b0
click 4,0 // Trigger Touch Release Event of component with .id 4
4	ref_stop	0	 Stops default waveform refreshing (will not refresh when data point added)
- waveform refreshing will resume with ref_star (see 3.5)
usage: ref_stop
ref_stop // stop refreshing the waveform on each data point added
5	ref_star	0	 Resume default waveform refreshing (refresh on data point add)
- used to resume waveform refreshing stopped by ref_stop (see 3.4)
usage: ref_start
ref_star // resume default refreshing, refresh on each data point added
6	get	1	 Send attribute/constant over serial (0x70/0x71 Return Data)
usage: get
is either numeric value, .txt contents, or constant
get t0.txt // sends text contents of t0.txt in 0x70 Return Data format
get "123" // sends text constant "123" in 0x70 Return Data format
get n0.val // sends numeric value of n0.val in 0x71 Return Data format
get 123 // sends numeric constant 123 in 0x71 Return Data format
7	sendme	0	 Sends number of currently loaded page over serial (0x66 Return Data)
- number of currently loaded page is stored in system variable dp
- used in a page's initialize event will auto-send as page loads
usage: sendme
sendme // sends the value of dp in 0x66 Return Data Format
8	covx	4	 Convert variable from numeric type to text, or text to numeric type
- text must be text ASCII representation of an integer value.
- source and destination types must not be of the same type
- when numeric source and hex format chosen, length must be even for bytewise hex digits (0, 2, 4, 6 or 8)
ie: (len 4) positive significant (byte 0 to 3), 123 = 0000007B = 007B
ie: (len 4) negative significant (byte 3 to 0), -123 = FFFFFF85 = FFFF
- value is more than allowed space results in a truncation
- it is recommended to ensure handling source length in user code before covx
- in v0.53, covx is deemed undefined if source is larger than length or
dest txt_maxl is smaller than requested length.
(some of these undefines, can be exploited)
ie: src numeric value of 123 with length 0, result is dest text "123"
- when length is fixed and value is less, leading zeros will be added
ie: src numeric value of 123 with length 4, result is dest text "0123"
- when value is larger than length, .txt truncated to least significant digits
ie: src numeric value of 23425 with length 4 result is dest text "2342"
usage: covx ,,,
is text attribute (or numeric attribute when is text)
is numeric attribute (or text attribute when is numeric)
will determine if leading zeros added to conversion to text
0: integer, 1: Comma separated 1,000s, 2: Hex
covx h0.val,t0.txt,0,0 // convert value of h0 into t0.txt without leading zeros
covx t0.txt,h0.val,0,0 // convert t0.txt into integer in h0.val ignored.
covx h0.val,t0.txt,4,0 // convert value of h0 into t0.txt with exactly 4 digits
covx h0.val,t0.txt,4,1 // convert value of h0 into t0.txt with commas
covx h0.val,t0.txt,4,2 // convert value of h0 into t0.txt in 4 hex digits (2 Bytes)
Invalid: covx h0.val,va0.val,0,0 or covx t0.txt,va0.txt,0,0 // src & dest same type.
8a	cov	3	 Depreciated. Convert from numeric type to text, or text to numeric type
- text must be text ASCII representation of an integer value.
- source and destination types must not be of the same type
- when length is fixed and value is less, leading zeros will be added
ie: src numeric value of 123 with length 4, result is dest text "0123"
- dest txt_maxl and length needs be large enough to accommodate conversion.
ie: src numeric value of 123 with length 0, result is dest text "123"
- when value is larger than length, .txt results in a truncation
- it is recommended to handle source length in user code before cov
Note:v0.53 changed behaviour from previous pre/post v0.53 behaviours.
cov is deemed undefined if source is larger than length or the dest txt_maxl is
smaller than the requested length. Some undefines are exploitable.
usage: cov ,,
is text attribute (or numeric attribute when is text)
is numeric attribute (or text attribute when is numeric)
will determine if leading zeros added to conversion to text
cov h0.val,t0.txt,0 // convert value of h0 into t0.txt without leading zeros
cov t0.txt,h0.val,0 // convert integer into t0.txt from h0.val ignored.
cov h0.val,t0.txt,4 // convert value of h0 into t0.txt with exactly 4 digits
Invalid: cov h0.val,va0.val,0 or cov t0.txt,va0.txt,0 // src & dest same type.
9	touch_j	0	 Recalibrate the Resistive Nextion device's touch sensor
- presents 4 points on screen for user to touch, saves, and then reboots.
- typically not required as device is calibrated at factory
- sensor can be effected by changes of conditions in environment
- Capacitive Nextion devices can not be user calibrated.
usage: touch_j
touch_j // trigger the recalibration of touch sensor
10	substr	4	 Extract character or characters from contents of a Text attribute
usage: substr ,,,
is text attribute where text will be extracted from
is text attribute to where extracted text will be placed
is start position for extraction (0 is first char, 1 second)
is the number of characters to extract
substr va0.txt,t0.txt,0,5 // extract first 5 chars from va0.txt, put into t0.txt
11	vis	2	 Hide or Show component on current page
- show will refresh component and bring it to the forefront layer
- hide will remove component visually, touch events will be disabled
- use layering with mindful purpose, can cause ripping and flickering.
- use with caution and mindful purpose, may lead to difficult debug session
usage: vis
is either .objname or .id of component on current page,
- valid .id is 0 - page, 1 to 250 if component exists, and 255 for all
is either 0 to hide, or 1 to show.
vis b0,0 // hide component with .objname b0
vis b0,1 // show component with .objname b0, refresh on front layer
vis 1,0 // hide component with .id 1
vis 1,1 // show component with .id 1, refresh on front layer
vis 255,0 // hides all components on the current page
12	tsw	2	 Enable or disable touch events for component on current page
- by default, all components are enabled unless disabled by tsw
- use with caution and mindful purpose, may lead to difficult debug session
usage: tsw
is either .objname or .id of component on current page,
- valid .id is 0 - page, 1 to 250 if component exists, and 255 for all
is either 0 to disable, or 1 to enable.
tsw b0,0 // disable Touch Press/Release events for component b0
tsw b0,1 // enable Touch Press/Release events for component b0
tsw 1,0 // disable Touch Press/Release events for component with id 1
tsw 1,1 // enable Touch Press/Release events for component with id 1
tsw 255,0 // disable all Touch Press/Release events on current page
13	com_stop	0	 Stop execution of instructions received from Serial
- Serial will continue to receive and store in buffer.
- execution of instructions from Serial will resume with com_star (see 3.14)
- using com_stop and com_star may cause a buffer overflow condition.
- Refer to device datasheet for buffer size and command queue size
usage: com_stop
com_stop // stops execution of instructions from Serial
14	com_star	0	 Resume execution of instructions received from Serial
- used to resume an execution stop caused by com_stop (see 3.13)
- when com_star received, all instructions in buffer will be executed
- using com_stop and com_star may cause a buffer overflow condition.
- Refer to device datasheet for buffer size and command queue size
usage: com_star
com_star // resume execution of instruction from Serial
15	randset	2	 Set the Random Generator Range for use with rand (see 6.14)
- range will persist until changed or Nextion rebooted
- set range to desired range before using rand
- power on default range is -2147483648 to 2147483647, runtime range is user definable.
usage: randset ,
is value is -2147483648 to 2147483647
is value greater than min and less than 2147483647
randset 1,100 //set current random generator range from 1 to 100
randset 0,65535 //set current random generator range from 0 to 65535
16	code_c	0	 Clear the commands/data queued in command buffer without execution
usage: code_c
code_c // Clears the command buffer without execution
17	prints	2	 Send raw formatted data over Serial to MCU
- prints does not use Nextion Return Data, user must handle MCU side
- qty of data may be limited by serial buffer (all data < 1024)
- numeric value sent in 4 byte 32-bit little endian order
value = byte1+byte2*256+byte3*65536+byte4*16777216
- text content sent is sent 1 ASCII byte per character, without null byte.
usage: prints ,
is either component attribute, variable or Constant
is either 0 (all) or number to limit the bytes to send.
prints t0.txt,0 // return 1 byte per char of t0.txt without null byte ending.
prints t0.txt,4 // returns first 4 bytes, 1 byte per char of t0.txt without null byte ending.
prints j0.val,0 // return 4 bytes for j0.val in little endian order
prints j0.val,1 // returns 1 byte of j0.val in little endian order
prints "123",2 // return 2 bytes for text "12" 0x31 0x32
prints 123,2 // returns 2 bytes for value 123 0x7B 0x00
17a	print	1	 Depreciated. Send raw formatted data over Serial to MCU
- print/printh does not use Nextion Return Data, user must handle MCU side
- qty of data may be limited by serial buffer (all data < 1024)
- numeric value sent in 4 byte 32-bit little endian order
value = byte1+byte2*256+byte3*65536+byte4*16777216
- text content sent is sent 1 ASCII byte per character, without null byte.
usage: print
is either component attribute, variable or Constant
print t0.txt // return 1 byte per char of t0.txt without null byte ending.
print j0.val // return 4 bytes for j0.val in little endian order
print "123" // return 3 bytes for text "123" 0x31 0x32 0x33
print 123 // return 4 bytes for value 123 0x7B 0x00 0x00 0x00
18	printh	1 to
many	 Send raw byte or multiple raw bytes over Serial to MCU
- printh is one of the few commands that parameter uses space char 0x20
- when more than one byte is being sent a space separates each byte
- byte is represented by 2 of (ASCII char of hexadecimal value per nibble)
- qty may be limited by serial buffer (all data < 1024)
- print/printh does not use Nextion Return Data, user must handle MCU side
usage: printh [
is hexadecimal value of each nibble. 0x34 as 34
is a space char 0x20, used to separate each pair
printh 0d // send single byte: value 13 hex: 0x0d
printh 0d 0a // send two bytes: value 13,10 hex: 0x0d0x0a
19	add	3	 Add single value to Waveform Channel
- waveform channel data range is min 0, max 255
- 1 pixel column is used per data value added
- y placement is if value < s0.h then s0.y+s0.h-value, otherwise s0.y
usage: add ,,
is the .id of the waveform component
is the channel the data will be added to
is ASCII text of data value, or numeric value
- valid: va0.val or sys0 or j0.val or 10
add 1,0,30 // add value 30 to Channel 0 of Waveform with .id 1
add 2,1,h0.val // add h0.val to Channel 1 of Waveform with .id 2
20	addt	3	 Add specified number of bytes to Waveform Channel over Serial from MCU
- waveform channel data range is min 0, max 255
- 1 pixel column is used per data value added.
- addt uses Transparent Data Mode (see 1.16)
- waveform will not refresh until Transparent Data Mode completes.
- qty limited by serial buffer (all commands+terminations + data < 1024)
- also refer to add command (see 3.19)
usage: add ,,
is the .id of the waveform component
is the channel the data will be added to
is the number of byte values to add to
addt 2,0,20 // adds 20 bytes to Channel 0 Waveform with .id 2 from serial
// byte of data is not ASCII text of byte value, but raw byte of data.
21	cle	3	 Clear waveform channel data
usage: cle ,
is the .id of the waveform component
is the channel to clear
must be a valid channel: < waveform.ch or 255
0 if .ch 1, 1 if .ch 2, 2 if .ch 3, 3 if .ch=4 and 255 (all channels)
cle 1,0 // clear channel 0 data from waveform with .id 1
cle 2,255 // clear all channels from waveform with .id 2
22	rest	0	 Resets the Nextion Device
usage: rest
rest // immediate reset of Nextion device - reboot.
23	doevents	0	 Force immediate screen refresh and receive serial bytes to buffer
- useful inside exclusive code block for visual refresh (see 3.26 and 3.27)
usage: doevents
doevents // allows refresh and serial to receive during code block
24	strlen	2	 Computes the length of string in and puts value in
usage: strlen ,
must be a string attribute ie: t0.txt, va0.txt
must be numeric ie: n0.val, sys0, va0.val
strlen t0.txt,n0.val // assigns n0.val with length of t0.txt content
24a	btlen	2	 Computes number of bytes string in uses and puts value in
usage: btlen ,
must be a string attribute ie: t0.txt, va0.txt
must be numeric ie: n0.val, sys0, va0.val
btlen t0.txt,n0.val // assigns n0.val with number of bytes t0.txt occupies
25	if	Block	 Conditionally execute code block if boolean condition is met
- execute commands within block if (conditions) is met.
- nested conditions using () is not allowed. invalid: ((h0.val+3)>0)
- block opening brace and else. valid: }else invalid: } else
- Text comparison supports ==, !=
- Numerical comparison supports >, <, ==, !=, >=, <=
- conditions can be joined with && or || with no spaces used
- nested "if" and "else if" supported.
usage: if condition block [else if condition block] [else block]
- (conditions) is a simple non-complex boolean comparison evaluating left to right
valid: (j0.val>75) invalid: (j0.val+1>75) invalid: (j0.val
if(t0.txt=="123456")


if(t0.txt=="123456"||sys0==14&&n0.val==12)


if(t0.txt=="123456"&&sys0!=14)


if(n0.val==123)
else


if(rtc==1)
else if(rtc1==2)
else if(rtc1==3)
else

26	while	Block	 Continually executes code block until boolean condition is no longer met
- tests boolean condition and execute commands within block if conditions was met and continues to re-execute block until condition is not met.
- nested conditions using () is not allowed. invalid: ((h0.val+3)>0)
- block opening brace { must be on line by itself
- Text comparison supports ==, !=
- Numerical comparison supports >, <, ==, !=, >=, <=
- conditions can be joined with && or || with no spaces used
- block runs exclusively until completion unless doevents used (see 3.23)
usage: while condition block
- (conditions) is a simple non-complex boolean comparison evaluating left to right
valid: (j0.val>75) invalid: (j0.val+1>75)
// increment n0.val as lon as n0.val < 100.  result: b0.val=100
// will not visually see n0.val increment, refresh when while-loop completes
while(n0.val<100)
{
  n0.val++
}

//increment n0.val as long as n0.val < 100. result: n0.val=100
// will visually see n0.val increment, refresh each evaluation of while-loop
while(n0.val<100)
{
  n0.val++
  doevents
}
27	for	Block	 Iterate execution of code block as long as boolean condition is met
- executes init_assignment, then tests boolean condition and executes
commands within block if boolean condition is met, when iteration of
block execution completes step_assignment is executed. Continues to
iterate block and step_assignment until boolean condition is not met.
- nested conditions using () is not allowed. invalid: ((h0.val+3)>0)
- block opening brace { must be on line by itself
- Text comparison supports ==, !=
- Numerical comparison supports >, <, ==, !=, >=, <=
- conditions can be joined with && or || with no spaces used
- block runs exclusively until completion unless doevents used (see 3.23)
usage: for(init_assignment;condition;step_assignment) block
- init_assignment and step_assignment are simple non-complex statement
valid: n0.val=4, sys2++, n0.val=sys2*4+3 invalid: n0.val=3+(sys2*4)-1
- (conditions) is a simple non-complex boolean comparison evaluating left to right
valid: (j0.val>75) invalid: (j0.val+1>75)
// iterate n0.val by 1's as long as n0.val<100. result: n0.val=100
// will not visually see n0val increment until for-loop completes
for(n0.val=0;n0.val<100;n0.val++)
{
}

////iterate n0.val by 2's as long as n0.val<100. result: n0.val=100
// will visually see n0.val increment when doevents processed
for(n0.val=0;n0.val<100;n0.val+=2)
{
  doevents
}
28	wepo	2	 Store value/string to EEPROM
- EEPROM valid address range is from 0 to 1023 (1K EEPROM)
- numeric value length: is 4 bytes, -2147483648 to 2147483647
- numeric data type signed long integer, stored in little endian order.
val[addr+3]*16777216+val[addr+2]*65536+val[addr+1]*256+val[addr] - string content length: .txt content is .txt-maxl +1, or constant length +1
usage: wepo ,
is variable or constant
is storage starting address in EEPROM
wepo t0.txt,10 // writes t0.txt contents in addresses 10 to 10+t0.txt-maxl
wepo "abcd",10 // write constant "abcd" in addresses 10 to 14
wepo 11,10 // write constant 11 in addresses 10 to 13
wepo n0.val,10 // write value n0.val in addresses 10 to 13
29	repo	2	 Read value from EEPROM
- EEPROM valid address range is from 0 to 1023 (1K EEPROM)
- numeric value length: is 4 bytes, -2147483648 to 2147483647
- numeric data type signed long integer, stored in little endian order.
val[addr+3]*16777216+val[addr+2]*65536+val[addr+1]*256+val[addr] - string content length: .txt content is lesser of .txt-maxl or until null reached.
usage: repo ,
is variable or constant
is storage starting address in EEPROM
repo t0.txt,10 // reads qty .txt-maxl chars (or until null) from 10 into t0.txt
repo n0.val,10 // reads 4 bytes from address 10 to 13 into n0.val
30	wept	2	 Store specified number of bytes to EEPROM over Serial from MCU
- EEPROM valid address range is from 0 to 1023 (1K EEPROM)
- wept uses Transparent Data Mode (see 1.16)
- qty limited by serial buffer (all commands+terminations + data < 1024)
usage: wept ,
is storage starting address in EEPROM
is the number of bytes to store
wept 30,20 // writes 20 bytes into EEPROM addresses 30 to 49 from serial
// byte of data is not ASCII text of byte value, but raw byte of data.
31	rept	2	 Read specified number of bytes from EEPROM over Serial to MCU
- EEPROM valid address range is from 0 to 1023 (1K EEPROM)
usage: rept ,
is storage starting address in EEPROM
is the number of bytes to read
rept 30,20 // sends 20 bytes from EEPROM addresses 30 to 49 to serial
// byte of data is not ASCII text of byte value, but raw byte of data.
32	cfgpio	3	 Configure Nextion GPIO
usage: cfgpio
is the number of the extended I/O pin.
- Valid values in PWM output mode: 4 to 7, all other modes 0 to 7.
is the working mode of pin selected by .
- Valid values: 0-pull up input, 1-input binding, 2-push pull output,
3-PWM output, 4-open drain output.
component .objname or .id when is 1 (otherwise use 0)
- in binding mode, cfgpio needs to be declared after every refresh of page to reconnect to Touch event, best to put cfgpio in page pre-initialization event
cfgpio 0,0,0 // configures io0 as a pull-up input. Read as n0.val=pio0.
cfgpio 1,2,0 // configures io1 as a push-pull output, write as pio1=1
cfgpio 2,1, b0 // configures io2 as binding input with current page b0.
// binding triggers b0 Press on falling edge and b0 Release on rising edge
For PWM mode, set duty cycle before cfgpio: ie: pwm4=20 for 20% duty.
cfgpio 4,3,0 // configures io4 as PWM output. pwmf=933 to change Hz.
// changing pwmf changes frequency of all configured PWM io4 to io7
33	ucopy	4	 Advanced. Read Only. Valid in active Protocol Reparse mode.
Copies data from the serial buffer.
When Nextion is in active Protocol Reparse mode, ucopy copies data from the serial buffer. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
usage: ucopy ,,,
must be a writeable attribute ie: t0.txt, va0.val
must be numeric value ie: 0
must be a numeric value ie: 4
must be numeric value ie: 0
ucopy n0.val,0,2,0 // copy buffer bytes 0,1 to lower 2 bytes of n0.val
ucopy n0.val,0,2,2 // copy buffer bytes 0,1 to upper 2 bytes of n0.val
ucopy n0.val,0,4,0 // copy buffer bytes 0,1,2,3 to n0.val
ucopy t0.txt,0,10,0 // copy buffer bytes 0 to 9 into t0.txt
34	move	7	 Move component.
usage: move ,,,,,,
is the component name or component id
is the starting X coordinate
is the starting Y coordinate
is the destination X coordinate
is the destination Y coordinate is a value 0 to 100, 100 being highest priority
is time in ms.
move t0,-30,-30,100,150,95,120 // 120ms to move t0 into position 100,150
move t1,-30,-30,200,150,90,180 // 180ms to move t1 into position 200,150
move t2,-30,-30,300,150,100,150 // 150ms to move t2 into position 300,150
// given the example priorities, t2 moves first, then t0 and lastly t1
35	play	3	 Play audio resource on selected Channel
usage: play ,,
is the channel 0 or 1
is the Audio Resource ID
is 0 for no looping, 1 to loopthe starting Y coordinate
Notes: The play instruction is used to configure and start audio playback. audio0 and audio1 are used to control the channel. Audio playback is global and playback continues after leaving and changing pages, if you want the audio to stop on leaving the page, you should do so in the page leave event
play 1,3,0// play resource 3 on channel 1 with no looping
play 0,2,1// play resource 2 on channel 0 with looping
36	twfile	2	 Advanced. Transfer file over Serial
usage: twfile ,
is destination path and filename quote encapsulated text
is the size of the file in bytes.
twfile "ram/0.jpg",1120// transfer jpg over serial to ram/0.jpg size 1120 bytes
twfile "sd0/0.jpg",1120// transfer jpg over serial to sd0/0.jpg size 1120 bytes
37	delfile	1	 Advanced. Delete external file.
usage: delfile
is target path and filename as quote encapsulated text
delfile "ram/0.jpg"// remove transferred file ram/0.jpg
delfile "sd0/0.jpg"// remove transferred file sd0/0.jpg
38	refile	2	 Advanced. Rename external file.
usage: refile ,
is source path and filename as quote encapsulated text
is target path and filename as quote encapsulated text
refile "ram/0.jpg","ram/1.jpg"// rename file ram/0.jpg to ram/1.jpg
refile "sd0/0.jpg","sd0/1.jpg"// rename file sd0/0.jpg to sd0/1.jpg
39	findfile	2	 Advanced. Find File reports if named external file exists
usage: findfile ,
is source path and filename as quote encapsulated text
is a numeric attribute for the result to be stored
Returns 0 result if find fails, returns 1 if find is successful.
findfile "ram/0.jpg",n0.val// check if file exists, store result in n0.val
findfile "sd0/0.jpg",sys0//check if file exists, store result in sys0
40	rdfile	4	 Advanced. Read File contents and outputs contents over serial
usage: rdfile ,,,
is source path and filename as quote encapsulated text
is the starting offset of the file
is number of bytes to return (see note if 0)
is an option (0: no crc, 1: Modbus crc16, 10: crc32)
If count is 0, then 4 byte file size is returned in little endian order.
rdfile "ram/0.jpg",0,10,0// send first 10 bytes of file, no CRC, 10 bytes.
rdfile "sd0/0.jpg",0,10,1// send first 10 bytes of file, MODBUS CRC, 12 bytes.
rdfile "sd0/0.jpg",0,10,10// send first 10 bytes of file, CRC32, 14 bytes.
41	setlayer	2	 Set Component Layer
usage: setlayer ,
is component ID or objname of component needing to change layers
is the component ID or object name comp1 is placed above
Note: using comp2 value of 255 places comp1 on topmost layer.
setlayer t0,n0//places to above n0's layer
setlayer t0,255//place t0 on the topmost layer
setlayer n0,3//place n0 on the 3rd layer
42	newdir	1	 Advanced. Create a new directory
usage: newdir
is directory to be created
Note: directory name to end with forward slash /
newdir "sd0/data/"//create directory called data
newdir "sd0/202003/"//create directory called 202003
43	deldir	1	 Advanced. Remove a directory
usage: deldir
is directory to be deleted
Note: directory name to end with forward slash /
deldir "sd0/data/"//remove directory called data
deldir "sd0/202003/"//remove directory called 202003
44	redir	2	 Advanced. Rename a directory
usage: redir ,
is directory to be renamed
new name of directory being renamed
Note: directory names to end with forward slash /
redir "sd0/data/","sd0/data2/"//rename data to data2
redir "sd0/202003/","sd0/2004/"//rename 202003 to 2004
45	finddir	2	 Advanced. Test if directory exists
usage: finddir
,
is directory to test if exists
number variable where result will be stored
Note: directory names to end with forward slash /
Returns 1 if directory exists, returns 0 if not found
finddir "sd0/data/",va0.val//find directory data, result in va0.val
finddir "sd0/2003/",sys0//find directory 2004, result in sys0
46	udelete	1	 Advanced. Remove bytes from Serial Buffer
usage: udelete
is number of bytes to remove from beginning of Serial Buffer
Note: Protocol Reparse Mode (recmod) must be active to be used. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
udelete 24//delete first 24 bytes of Buffer
udelete 10//delete first 10 bytes of Buffer
47	crcrest	2	 Advanced. Reset CRC and Initialize
usage: crcrest ,
must be 1 (type Modbus CRC16)
is crc initial value (usually 0xFFFF)
crcrest 1,0xFFFF//reset and initialize crc
48	crcputs	2	 Advanced. Accumulate CRC for Variable or constant
usage: crcputs ,
is attribute or constant
is 0 (for Automatic) or specified length
crcputs va0.val,0//accumulate crc for va0.val (length auto)
crcputs va1.txt,3//accumulate crc for first 3 bytes of va1.txt
49	crcputh	1	 Advanced. Accumulate CRC for hex string
usage: crcputh
is string of hex chars
Note: each byte in the hex string has 2 hexdigits, bytes separated by a space.
crcputh 0A//accumulate crc for byte 0x0A
crcputh 0A 0D//accumulate crc for bytes 0x0A 0x0D
50	crcputu	2	 Advanced. Accumulate CRC on Serial Buffer
usage: crcputu ,
is start byte of Serial Buffer to accumulate
is number of bytes to accumulate including start byte
Note: Protocol Reparse Mode (recmod) must be active to be used. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
crcputu 0,10//accumulate crc for first 10 bytes of Serial Buffer
crcputu 10,10//accumulate crc for second 10 bytes 0x0A 0x0D
51	spstr	4	 Split String
usage: spstr ,,,
is src .txt attribute or string data constant
is .txt attribute where result is stored
is the text delimiter encapsulated in double quotes
is zero-indexed iteration result to return
spstr "ab3cd3ef3ghi",va1.txt,"3",0//return string ab before first delimiter occurs
spstr "ab3cd3ef3ghi",va1.txt,"3",2//return string ef after second delimiter occurs
52	newfile	2	 Advanced. Create an external file (ram or microSD).
usage: newfile ,
is target path and filename as quote encapsulated text
is size of file (in bytes) reserved for file content
newfile "ram/0.bin",512 // create ram file 0.jpg with filesize of 512 bytes
newfile "sd0/0.bin",512 // create microSD file 0.jpg with filesize of 512 bytes
Notes: To create a file in ram, an amount of ram for file space must first be set aside in the Memory file storage size field in your Device > Setting window > Project tab. Files in ram that are no longer used should be removed with the delfile instruction to avoid running out of file space. While the twfile instruction is still used to create and fill the file content over Serial, the newfile instruction only declares the file name and reserves the space.
----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
System Variables
No.	Name	Meaning	Example/Description
1	dp	Current
Page ID	dp=1, n0.val=dp
 read: Contains the current page displayed as per the HMI design
write: change page to value specified (same effect as page command)
min 0, max # of highest existing page in the user's HMI design.
2	dim
dims	Nextion
Backlight	dim=32, dims=100
 Sets the backlight level in percent
min 0, max 100, default 100 or user defined
Note: dim=32 will set the current backlight level to 32%.
using dims=32 will set the current backlight level to 32% and save this to be new power on default backlight level, persisting until changed.
3	baud
bauds	Nextion
Baud Rate	baud=9600, bauds=9600
 Sets the Nextion Baud rate in bits-per-second
min 2400, max 921600, default 9600 or user defined
Valid values are: 2400, 4800, 9600, 19200, 31250, 38400, 57600, and 115200, 230400, 250000, 256000, 512000, and 921600
Note: baud=38400 will set the current baud rate to 38400
using bauds=38400 will set the current baud rate to 38400 and save this to be new power on default baud rate, persisting until changed.
Note: on rare occasions bauds has become lost. With the addition of the Editor Program.s feature, it is now recommended to specify your desired baud rate baud=9600 between declarations and before the page0 instruction and no longer recommending inserting bauds=9600 in the first page's Preinitialization Event of the HMI.
4	spax
spay	Font
Spacing	spax=2, spay=2
 Sets the default rendering space for xstr:
horizontally between font characters with spax additional pixels and
vertically between rows (if multi-lined) with spay additional pixels.
min 0, max 65535, default 0
Note: Components now have their own individual .spax/.spay attributes that are now used to determine spacing for the individual component.
5	thc	Touch
Draw Brush
Color	thc=RED, thc=1024
 Sets the Touch Drawing brush color
min 0, max 65535, default 0
Valid choices are either color constants or the decimal 565 color value.
6	thdra	Touch
Drawing	thdra=1 (on), thdra=0 (off)
 Turns the internal drawing function on or off.
min 0, max 1, default 0
When the drawing function is on, Nextion will follow touch dragging with the current brush color (as determined by the thc variable).
7	ussp	Sleep on
No Serial	ussp=30
 Sets internal No-serial-then-sleep timer to specified value in seconds
min 3, max 65535, default 0 (max: 18 hours 12 minutes 15 seconds)
Nextion will auto-enter sleep mode if and when this timer expires.
Note: Nextion device needs to exit sleep to issue ussp=0 to disable sleep on no serial, otherwise once ussp is set, it will persist until reboot or reset.
8	thsp	Sleep on
No Touch	thsp=30
 Sets internal No-touch-then-sleep timer to specified value in seconds
min 3, max 65535, default 0 (max: 18 hours 12 minutes 15 seconds)
Nextion will auto-enter sleep mode if and when this timer expires.
Note: Nextion device needs to exit sleep to issue thsp=0 to disable sleep on no touch, otherwise once thsp is set, it will persist until reboot or reset.
9	thup	Auto Wake
on Touch	thup=0 (do not wake), thup=1 (wake on touch)
 Sets if Nextion should auto-wake from sleep when touch press occurs.
min 0, max 1, default 0
When value is 1 and Nextion is in sleep mode, the first touch will only trigger the auto wake mode and not trigger a Touch Event.
thup has no influence on sendxy, sendxy will operate independently.
10	sendxy	RealTime
Touch
Coordinates	sendxy=1 (start sending) sendxy=0 (stop sending)
 Sets if Nextion should send 0x67 and 0x68 Return Data
min 0, max 1, default 0
- Less accurate closer to edges, and more accurate closer to center.
Note: expecting exact pixel (0,0) or (799,479) is simply not achievable.
11	delay	Delay	delay=100
 Creates a halt in Nextion code execution for specified time in ms
min 0, max 65535
As delay is interpreted, a total halt is avoided. Incoming serial data is received and stored in buffer but not be processed until delay ends. If delay of more than 65.535 seconds is required, use of multiple delay statements required.
delay=-1 is max. 65.535 seconds.
12	sleep	Sleep	sleep=1 (Enter sleep mode) or sleep=0 (Exit sleep mode)
 Sets Nextion mode between sleep and awake.
min 0, max 1, or default 0
When exiting sleep mode, the Nextion device will auto refresh the page
(as determined by the value in the wup variable) and reset the backlight brightness (as determined by the value in the dim variable). A get/print/printh/wup/sleep instruction can be executed during sleep mode. Extended IO binding interrupts do not occur in sleep.
13	bkcmd	Pass / Fail
Return Data	bkcmd=3
 Sets the level of Return Data on commands processed over Serial.
min 0, max 3, default 2
- Level 0 is Off - no pass/fail will be returned
- Level 1 is OnSuccess, only when last serial command successful.
- Level 2 is OnFailure, only when last serial command failed
- Level 3 is Always, returns 0x00 to 0x23 result of serial command.
Result is only sent after serial command/task has been completed, as such this provides an invaluable status for debugging and branching. Table 2 of Section 7 Nextion Return Data is not subject to bkcmd
14	rand	Random
Value	n0.val=rand
 Readonly. Value returned by rand is random every time it is referred to.
default range is -2147483648 to 2147483647
range of rand is user customizable using the randset command
range as set with randset will persist until reboot or reset
15	sys0
sys1
sys2	Numeric
System
Variables	sys0=10 sys1=40 sys2=60 n0.val=sys2
 System Variables are global in nature with no need to define or create.
They can be read or written from any page. 32-bit signed integers.
min value of -2147483648, max value of 2147483647
Suggested uses of sys variables include
- as temporary variables in complex calculations
- as parameters to pass to click function or pass between pages.
16	wup	Wake Up
Page	wup=2, n0.val=wup
 Sets which page Nextion loads when exiting sleep mode
min is 0, max is # of last page in HMI, or default 255
When wup=255 (not set to any existing page)
- Nextion wakes up to current page, refreshing components only
wup can be set even when Nextion is in sleep mode
17	usup	Wake On
Serial Data	usup=0, usup=1
 Sets if serial data wakes Nextion from sleep mode automatically.
min is 0, max is 1, default 0
When usup=0, send sleep=0ÿÿÿ to wake Nextion
When usup=1, any serial received wakes Nextion
18	rtc0
rtc1
rtc2
rtc3
rtc4
rtc5
rtc6	RTC	rtc0=2017, rtc1=8, rtc2=28,
rtc3=16, rtc4=50, rtc5=36, n0.val=rtc6
 Nextion RTC:
rtc0 is year 2000 to 2099, rtc1 is month 1 to 12, rtc2 is day 1 to 31,
rtc3 is hour 0 to 23, rtc4 is minute 0 to 59, rtc5 is second 0 to 59.
rtc6 is dayofweek 0 to 6 (Sunday=0, Saturday=6)
rtc6 is readonly and calculated by RTC when date is valid.
19	pio0
pio1
pio2
pio3
pio4
pio5
pio6
pio7	GPIO	pio3=1, pio3=0, n0.val=pio3
 Default mode when power on: pull up input mode
Internal pull up resistor. GPIO voltage level is 3.3V and previous documentation stated as 50K (Ω) Pullup resistance, is defined now in a bit more detail. As with any MCU there is short period of uncertain level on Power On between when the MCU can begin default pullup assertions, and after this then the user configuration is asserted (cfgpio instructions). For the smaller Enhanced Series (K024,K028,K032 @ 48MHz): Pullup resistance on the Enhanced is more precisely defined as Typical 40kΩ with Min 25kΩ Max 55kΩ. For the larger Enhanced Series (K035,K043,K050,K070 @ 108MHz): Pullup resistance on the larger Enhanced is more precisely defined as Typical 40kΩ with Min 30kΩ Max 50kΩ. For the Intelligent Series (all models @ 200Mhz): the pullup resistance is more precisely defined as Typical 66kΩ with Min 53kΩ and Max 120kΩ.
GPIO is digital. Value of 0 or 1 only.
- refer to cfgpio command for setting GPIO mode
read if in input mode, write if in output mode
19	pwm4
pwm5
pwm6
pwm7	PWM Duty
Cycle	pwm7=25
 Value in percentage. min 0, max 100, default 50.
- refer to cfgpio command for setting GPIO mode
 supports pwm4, pwm5, pwm6 and pwm7
 supports only pwm6 and pwm7
21	pwmf	PWM
Frequency	pwmf=933
 Value is in Hz. min value 1 Hz, max value 65535 Hz. default 1000 Hz
All PWM output is unified to only one Frequency, no independent individual settings are allowed.
- refer to cfgpio command for setting GPIO mode
22	addr	Address	addr=257
 Advanced. Enables/disables Nextion's two byte Address Mode
0, or min value 256, max value 2815. default 0
Setting addr will persist to be the new power-on default.
- refer to section 1.19
23	tch0
tch1
tch2
tch3	Touch
Coordinates	x.val=tch0, y.val=tch1
 Readonly. When Pressed tch0 is x coordinate, tch1 is y coordinate.
When released (not currently pressed), tch0 and tch1 will be 0.
tch2 holds the last x coordinate, tch3 holds the last y coordinate.
24	recmod	Protocol Reparse	recmod=0, recmod=1
 Advanced. Set passive or active Protocol Reparse mode.
min is 0, max is 1, default 0
When recmod=0, Nextion is in passive mode and processes serial data according to the Nextion Instruction Set, this is the default power on processing. When recmod=1, Nextion enters into active mode where the serial data waits to be processed by event code. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
25	usize	Bytes in Serial Buffer	n0.val=usize
 Advanced. Read Only. Valid in active Protocol Reparse mode.
min is 0, max is 1024
When Nextion is in active Protocol Reparse mode, usize reports the number of available bytes in the serial buffer. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
26	u[index]	Serial Buffer Data	n0.val=u[0]
 Advanced. Read Only. Valid in active Protocol Reparse mode.
min is 0, max is 255
When Nextion is in active Protocol Reparse mode, the u[index] array returns the byte at position index from the serial buffer. Most HMI applications will not require Protocol Reparse and should be skipped if not fully understood.
27	eql
eqm
eqh	Equalizer Groupings	eqm=7
 Valid on Nextion Device, not supported in Debug Simulator.
min is 0, max is 15
eql: Bass (31Hz to 125Hz, eq0..eq2)
eqm: Midrange (250Hz to 2000Hz, eq3..eq6)
eqh: Treble (4000Hz to 1600Hz, eq7..eq9)
Setting to 7 is Balanced with no attenuation, no gain
Setting 0..6, the lower the value the higher the attenuation
Setting 8..15, the higher the value the higher the gain
NOTE: The base of the equalizer is operated according to eq0..eq9,
when a group is modified the corresponding individual bands are modified, however modifying an individual band does not modify the group. (ie: setting eql=4 sets eq0, eq1 and eq2 to 4, but setting eq1=3 does not modify eql to 3, eq0 and eq2 remain at 4).
28	eq0
eq1
eq2
eq3
eq4
eq5
eq6
eq7
eq8
eq9	Equalizer
Individual
Bands	eq6=7
 Valid on Nextion Device, not supported in Debug Simulator.
min is 0, max is 15
eq0 (31Hz), eq1 (62Hz), eq2 (125Hz),
eq3 (250Hz), eq4 (500Hz), eq5 (1000Hz), eq6 (2000Hz),
eq7 (4000Hz), eq8 (8000Hz), eq9 (16000Hz)
Setting to 7 is Balanced with no attenuation, no gain
Setting 0..6, the lower the value the higher the attenuation
Setting 8..15, the higher the value the higher the gain
NOTE: The base of the equalizer is operated according to eq0..eq9,
when a group is modified the corresponding individual bands are modified, however modifying an individual band does not modify the group. (ie: setting eql=4 sets eq0, eq1 and eq2 to 4, but setting eq1=3 does not modify eql to 3, eq0 and eq2 remain at 4).
29	volume	Audio Volume	volume=60
 Valid on Nextion Device, not supported in Debug Simulator.
min is 0, max is 100
volume persists and sets the power-on default setting for the audio volume
30	audio0
audio1	Audio Channel Control	audio0=0// stop channel 0 audio playback

min is 0, max is 2
0 (stop), 1 (resume), 2 (pause).
Notes: The play instruction is used to configure and start audio playback. audio0 and audio1 are only used to control the channel. Only if the channel is paused can it be resumed, if the channel is stopped then the play instruction is required to start it again. Audio playback is global and playback continues after leaving and changing pages, if you want the channel to stop on leaving the page, you must do so in the page leave event
31	crcval	CRC Value	x.val=crcval
 Readonly. Holds the current CRC accumulated value.
Use crcrest to reset and initialize
Use crcputs, crcputh or crcputu to accumulate
32	lowpower	Low Power	Discovery Series. Low Power 0.25mA deep sleep
 Sets if Nextion should enter deep sleep mode on sleep command.
min 0, max 1, default 0
lowpower=0 (normal operations), lowpower=1 (deep sleep enabled)
In deep sleep mode, the wake-up time will be longer, the data will likely be lost when the serial power is receivng the wake-up command. It is recommended to send an empty command (termination NIS 1.1) and wait 500ms before operations.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Format of Nextion Return Data
Return Codes dependent on bkcmd value being greater than 0
No.	Byte	bkcmd	len	Meaning	Format/Description
1	0x00
	2,3	4	Invalid Instruction	0x00 0xFF 0xFF 0xFF
Returned when instruction sent by user has failed
2	0x01
	1,3	4	Instruction Successful	0x01 0xFF 0xFF 0xFF
Returned when instruction sent by user was successful
3	0x02
	2,3	4	Invalid Component ID	0x02 0xFF 0xFF 0xFF
Returned when invalid Component ID or name was used
4	0x03
	2,3	4	Invalid Page ID	0x03 0xFF 0xFF 0xFF
Returned when invalid Page ID or name was used
5	0x04
	2,3	4	Invalid Picture ID	0x04 0xFF 0xFF 0xFF
Returned when invalid Picture ID was used
6	0x05
	2,3	4	Invalid Font ID	0x05 0xFF 0xFF 0xFF
Returned when invalid Font ID was used
7	0x06
	2,3	4	Invalid File Operation	0x06 0xFF 0xFF 0xFF
Returned when File operation fails
8	0x09
	2,3	4	Invalid CRC	0x09 0xFF 0xFF 0xFF
Returned when Instructions with CRC validation fails their CRC check.
When using instructions with CRC termination (0xFE 0xFE 0xFE) and an incorrectly calculated CRC is sent with the Instruction, this error will be returned regardless of the bkcmd setting
9	0x11
	2,3	4	Invalid Baud rate Setting	0x11 0xFF 0xFF 0xFF
Returned when invalid Baud rate was used
10	0x12
	2,3	4	Invalid Waveform ID or Channel #	0x12 0xFF 0xFF 0xFF
Returned when invalid Waveform ID or Channel # was used
11	0x1A
	2,3	4	Invalid Variable name or attribute	0x1A 0xFF 0xFF 0xFF
Returned when invalid Variable name or invalid attribute was used
12	0x1B
	2,3	4	Invalid Variable Operation	0x1B 0xFF 0xFF 0xFF
Returned when Operation of Variable is invalid. ie: Text assignment t0.txt=abc or t0.txt=23, Numeric assignment j0.val="50" or j0.val=abc
13	0x1C
	2,3	4	Assignment failed to assign	0x1C 0xFF 0xFF 0xFF
Returned when attribute assignment failed to assign
14	0x1D
	2,3	4	EEPROM Operation failed	0x1D 0xFF 0xFF 0xFF
Returned when an EEPROM Operation has failed
15	0x1E
	2,3	4	Invalid Quantity of Parameters	0x1E 0xFF 0xFF 0xFF
Returned when the number of instruction parameters is invalid
16	0x1F
	2,3	4	IO Operation failed	0x1F 0xFF 0xFF 0xFF
Returned when an IO operation has failed
17	0x20
	2,3	4	Escape Character Invalid	0x20 0xFF 0xFF 0xFF
Returned when an unsupported escape character is used
18	0x23
	2,3	4	Variable name too long	0x23 0xFF 0xFF 0xFF
Returned when variable name is too long. Max length is 29 characters: 14 for page + "." + 14 for component.

Return Codes not affected by bkcmd value, valid in all cases
No.	Byte	length	Meaning	Format/Description
19	0x00
	6	Nextion Startup	0x00 0x00 0x00 0xFF 0xFF 0xFF
Returned when Nextion has started or reset. Since Nextion Editor v1.65.0, the Startup preamble is not at the firmware level but has been moved to a printh statement in Program.s allowing a user to keep, modify or remove as they choose.
20	0x24
	4	Serial Buffer Overflow	0x24 0xFF 0xFF 0xFF
Returned when a Serial Buffer overflow occurs
Buffer will continue to receive the current instruction, all previous instructions are lost.
21	0x65
	7	Touch Event	0x65 0x00 0x01 0x01 0xFF 0xFF 0xFF
Returned when Touch occurs and component's
corresponding Send Component ID is checked
in the users HMI design.
0x00 is page number, 0x01 is component ID,
0x01 is event (0x01 Press and 0x00 Release)
data: Page 0, Component 1, Pressed
22	0x66
	5	Current Page Number	0x66 0x01 0xFF 0xFF 0xFF
Returned when the sendme command is used.
0x01 is current page number
data: page 1
23	0x67
	9	Touch Coordinate (awake)	0x67 0x00 0x7A 0x00 0x1E 0x01 0xFF 0xFF 0xFF
Returned when sendxy=1 and not in sleep mode
0x00 0x7A is x coordinate in big endian order,
0x00 0x1E is y coordinate in big endian order,
0x01 is event (0x01 Press and 0x00 Release)
(0x00*256+0x71,0x00*256+0x1E)
data: (122,30) Pressed
24	0x68
	9	Touch Coordinate (sleep)	0x68 0x00 0x7A 0x00 0x1E 0x01 0xFF 0xFF 0xFF
Returned when sendxy=1 and exiting sleep
0x00 0x7A is x coordinate in big endian order,
0x00 0x1E is y coordinate in big endian order,
0x01 is event (0x01 Press and 0x00 Release)
(0x00*256+0x71,0x00*256+0x1E)
data: (122,30) Pressed
25	0x70
	Varied	String Data Enclosed	0x70 0x61 0x62 0x31 0x32 0x33 0xFF 0xFF 0xFF
Returned when using get command for a string.
Each byte is converted to char.
data: ab123
26	0x71
	8	Numeric Data Enclosed	0x71 0x01 0x02 0x03 0x04 0xFF 0xFF 0xFF
Returned when get command to return a number
4 byte 32-bit value in little endian order.
(0x01+0x02*256+0x03*65536+0x04*16777216)
data: 67305985
27	0x86
	4	Auto Entered Sleep Mode	0x86 0xFF 0xFF 0xFF
Returned when Nextion enters sleep automatically
Using sleep=1 will not return an 0x86
28	0x87
	4	Auto Wake from Sleep	0x87 0xFF 0xFF 0xFF
Returned when Nextion leaves sleep automatically
Using sleep=0 will not return an 0x87
29	0x88
	4	Nextion Ready	0x88 0xFF 0xFF 0xFF
Returned when Nextion has powered up and is now initialized successfully. Since Nextion Editor v1.65.0, the Nextion Ready is not at the firmware level but has been moved to a printh statement in Program.s allowing a user to keep, modify or remove as they choose.
30	0x89
	4	Start microSD Upgrade	0x89 0xFF 0xFF 0xFF
Returned when power on detects inserted microSD
and begins Upgrade by microSD process
31	0xFD
	4	Transparent Data Finished	0xFD 0xFF 0xFF 0xFF
Returned when all requested bytes of Transparent
Data mode have been received, and is now leaving transparent data mode (see 1.16)
32	0xFE
	4	Transparent Data Ready	0xFE 0xFF 0xFF 0xFF
Returned when requesting Transparent Data
mode, and device is now ready to begin receiving
the specified quantity of data (see 1.16)