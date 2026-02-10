Nextion Elements Documentation

**IMPORTANT FOR DEVELOPERS:** Always ask the user for component IDs before coding! Component IDs are assigned by Nextion Editor based on creation order and cannot be assumed.

---

Main Page

- preinit
	- Explanation: Hide error overlay.
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
- progNameDisp
	- Explanation: TODO
	- Nextion code: TODO
- graphDisp (ID=6)
	- Explanation: Waveform graph. ESP32 renders projected program curve into channel 0.
	- Nextion code: TODO (ESP32 uses cle/add)
- timeElapsed
	- Explanation: TODO
	- Nextion code: TODO
- timeRamaining
	- Explanation: TODO
	- Nextion code: TODO
- currentTemp
	- Explanation: TODO
	- Nextion code: TODO
- currentKw
	- Explanation: Displays current power (kW) placeholder from ESP32.
	- Nextion code: None (ESP32 sets via currentKw.txt="value")
- addProg
	- Explanation: Start a new program (no overwrite). ESP32 handles page change.
	- Nextion code: Touch Release Event
		prints "add_prog",0
		printh 0d 0a
- progBwsr
	- Explanation: FileBrowser selection. Sends selected filename to ESP32 for loading and graph render.
	- Nextion code: Touch Release Event
		prints "prog_select:",0
		prints progBwsr.txt,0
		printh 0d 0a
- settingsPageB
	- Explanation: Navigate to Settings page. Nextion only emits an event; ESP32 handles page change.
	- Nextion code: Touch Release Event
		prints "nav:settings",0
		printh 0d 0a
- clockDisplay
	- Explanation: Displays current time from Nextion RTC. Updated by tm0 timer every second.
	- Nextion code: None (updated by timer)

- dateDisplay
	- Explanation: Displays current date from Nextion RTC. Updated by tm0 timer every second.
	- Nextion code: None (updated by timer)

- clockTimer (Timer, interval=1000ms, enabled=1)
	- Explanation: Updates clock and date display from Nextion's built-in RTC every second. Uses clockVar (va1) as text buffer.
	- Nextion code: Timer Event
		// Format time as HH:MM:SS
		// Hour
		if(rtc3<10)
		{
		  clockDisplay.txt="0"
		}else
		{
		  clockDisplay.txt=""
		}
		cov rtc3,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		clockDisplay.txt+=":"
		// Minute
		if(rtc4<10)
		{
		  clockDisplay.txt+="0"
		}
		cov rtc4,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		clockDisplay.txt+=":"
		// Second
		if(rtc5<10)
		{
		  clockDisplay.txt+="0"
		}
		cov rtc5,clockVar.txt,0
		clockDisplay.txt+=clockVar.txt
		// Format date as DD/MM/YYYY
		// Day
		if(rtc2<10)
		{
		  dateDisplay.txt="0"
		}else
		{
		  dateDisplay.txt=""
		}
		cov rtc2,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt
		dateDisplay.txt+="/"
		// Month
		if(rtc1<10)
		{
		  dateDisplay.txt+="0"
		}
		cov rtc1,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt
		dateDisplay.txt+="/"
		// Year
		cov rtc0,clockVar.txt,0
		dateDisplay.txt+=clockVar.txt

- clockVar (Variable va1, type=string, txt_maxl=10, vscope=global)
	- Explanation: Text buffer for clock/date timer formatting.
	- Nextion code: None
- startProgBtn
	- Explanation: TODO
	- Nextion code: Touch Release Event
		prints "prog_start",0
		printh 0d 0a
- pauseProgB
	- Explanation: TODO
	- Nextion code: Touch Release Event
		prints "prog_pause",0
		printh 0d 0a
- endProgBtn
	- Explanation: TODO
	- Nextion code: Touch Release Event
		prints "prog_stop",0
		printh 0d 0a
- editProgBtn
	- Explanation: Edit currently loaded program (allow overwrite).
	- Nextion code: Touch Release Event
		prints "edit_prog:",0
		prints progNameDisp.txt,0
		printh 0d 0a

Settings Page

ESP32 sends cfg_* (hardware limits) values on page init.
User config overrides are removed.
Time/date fields read from Nextion RTC and are only saved if user edits them (tracked by timeDirty variable).

- timeDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited time fields (hour/min/sec). 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by hour/min/sec touch events, sent with save, reset on preinit)

- dateDirty (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if user edited date fields (day/month/year). 0=not edited, 1=edited. MUST be global scope.
	- Nextion code: None (set by day/month/year touch events, sent with save, reset on preinit)

- timeInit (Variable, type=int, val=0, vscope=global)
	- Explanation: Tracks if time fields were populated this page visit. MUST be global scope to persist through keyboard popup.
	- Nextion code: None (checked/set in preinit)

- settingsVar (Variable, type=string, txt_maxl=10, vscope=local)
	- Explanation: Text buffer for settings page operations (converting timeDirty to text for send).
	- Nextion code: None

- preinit
	- Explanation: Hide error overlay. Only populate time fields once per page visit (check timeInit flag).
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
		// Only populate time fields on first preinit (page entry)
		if(timeInit.val==0)
		{
		  timeDirty.val=0
		  dateDirty.val=0
		  cov rtc3,hourInput.txt,0
		  cov rtc4,minutesInput.txt,0
		  cov rtc5,secondsInput.txt,0
		  cov rtc2,dayInput.txt,0
		  cov rtc1,monthInput.txt,0
		  cov rtc0,yearInput.txt,0
		  timeInit.val=1
		}

- postinit
	- Explanation: No code needed. ESP32 sends config values after page navigation (triggered by nav:settings handler).
	- Nextion code: None (leave empty to avoid re-init on keyboard popup)

- pageExit (use Page Exit Event or backBtn)
	- Explanation: Reset timeInit flag when leaving page so it re-populates on next visit.
	- Note: Add timeInit.val=0 to backBtn Touch Release Event before prints

- backBtn
	- Explanation: Navigate back to Main page. Resets timeInit flag for next visit.
	- Nextion code: Touch Release Event
		timeInit.val=0
		prints "nav:main",0
		printh 0d 0a

- tempScaleBtn
	- Explanation: Toggle temperature scale (C/F). Local HMI logic only.
	- Nextion code: TODO (local toggle, no ESP32 event)

- saveSpecsB
	- Explanation: Send time/date values to ESP32. Includes timeDirty and dateDirty flags as ASCII.
	- Nextion code: Touch Release Event
		prints "save_settings:",0
		cov timeDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		cov dateDirty.val,settingsVar.txt,0
		prints settingsVar.txt,0
		printh 2c
		prints hourInput.txt,0
		printh 2c
		prints minutesInput.txt,0
		printh 2c
		prints secondsInput.txt,0
		printh 2c
		prints dayInput.txt,0
		printh 2c
		prints monthInput.txt,0
		printh 2c
		prints yearInput.txt,0
		printh 0d 0a

- cfg_t
	- Explanation: Read-only. Displays max operational time limit (minutes). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_t.txt="value")

- cfg_Tmax
	- Explanation: Read-only. Displays max temperature limit (°C). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_Tmax.txt="value")

- cfg_dt
	- Explanation: Read-only. Displays min sensor read frequency (seconds). Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_dt.txt="value")

- cfg_dTmax
	- Explanation: Read-only. Displays max delta-T per minute. Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_dTmax.txt="value")

- cfg_Power
	- Explanation: Read-only. Displays max power the device heating element can output in KW. Set by ESP32 on init.
	- Nextion code: None (ESP32 sets via cfg_Power.txt="value")

- hourInput
	- Explanation: RTC hour input (00-23). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- minutesInput
	- Explanation: RTC minutes input (00-59). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- secondsInput
	- Explanation: RTC seconds input (00-59). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		timeDirty.val=1

- dayInput
	- Explanation: RTC day input (01-31). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- monthInput
	- Explanation: RTC month input (01-12). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- yearInput
	- Explanation: RTC year input (2000-2099). Populated from RTC on preinit, user edits.
	- Nextion code: Touch Press Event
		dateDirty.val=1

- websiteQr
	- Explanation: QR code for website. Static display element.
	- Nextion code: None

- wirelessCfgB
	- Explanation: WiFi configuration button. TODO: future feature.
	- Nextion code: TODO

- bluetoothCfgB
	- Explanation: Bluetooth configuration button. TODO: future feature.
	- Nextion code: TODO

Programs Page

Method 2 Architecture:
- ESP32 holds program draft in memory (15 stages total)
- Only 5 stage fields exist on HMI (bStg1-5, t1-5, etc.)
- Page buttons send current page data to ESP32, which stores it and sends back new page data
- Save/Graph buttons send current page data; ESP32 merges with draft for full program

- preinit
	- Explanation: Hide error overlay, graph, and confirmation dialog on page entry.
	- Nextion code: Preinitialize Event
		vis errTxtHead,0
		vis errText,0
		vis errTxtCloseB,0
		vis graphDisp,0
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmDelete,0
		vis confirmCancel,0

- backToMain
	- Explanation: Navigate back to Main page.
	- Nextion code: Touch Release Event
		prints "nav:main",0
		printh 0d 0a

- bPrevP
	- Explanation: Navigate to previous page. Sends current page data to ESP32 for sync.
	- Nextion code: Touch Release Event
		prints "prog_page_data:prev,",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- pageNum
	- Explanation: Displays current page (1-3). Set by ESP32.
	- Nextion code: None (ESP32 sets via pageNum.txt="X")

- bNextP
	- Explanation: Navigate to next page. Sends current page data to ESP32 for sync.
	- Nextion code: Touch Release Event
		prints "prog_page_data:next,",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- progNameInput
	- Explanation: Program name text input field.
	- Nextion code: None

- tempScaleBtn
	- Explanation: TODO
	- Nextion code: TODO


- showGraph
	- Explanation: Show graph preview. Sends current page data; ESP32 merges with draft and renders full graph.
	- Nextion code: Touch Release Event
		prints "show_graph:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- saveProgBtn
	- Explanation: Validate and save program. Sends current page data; ESP32 merges with draft and saves.
	- Nextion code: Touch Release Event
		prints "save_prog:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- bStg1-5
	- Explanation: Stage number labels. ESP32 updates these when changing pages (1-5, 6-10, 11-15).
	- Nextion code: None (ESP32 sets via bStgX.txt="Y")

- t1-5
	- Explanation: Time input fields for current page's 5 stages.
	- Nextion code: None

- targetTMax1-5
	- Explanation: Target temperature fields for current page's 5 stages.
	- Nextion code: None

- tDelta1-5
	- Explanation: Time delta fields for current page's 5 stages.
	- Nextion code: None

- tempDelta1-5
	- Explanation: Temperature delta fields for current page's 5 stages.
	- Nextion code: None

- autofillBtn
	- Explanation: Calculate missing time or delta_T fields. Requires target_temp + (time OR delta_T).
	- Formulas: t = T / delta_T, or delta_T = T / t, where T = target_T - current_T
	- Nextion code: Touch Release Event
		prints "autofill:",0
		prints progNameInput.txt,0
		printh 2c
		prints bStg1.txt,0
		printh 2c
		prints t1.txt,0
		printh 2c
		prints targetTMax1.txt,0
		printh 2c
		prints tDelta1.txt,0
		printh 2c
		prints tempDelta1.txt,0
		printh 2c
		prints bStg2.txt,0
		printh 2c
		prints t2.txt,0
		printh 2c
		prints targetTMax2.txt,0
		printh 2c
		prints tDelta2.txt,0
		printh 2c
		prints tempDelta2.txt,0
		printh 2c
		prints bStg3.txt,0
		printh 2c
		prints t3.txt,0
		printh 2c
		prints targetTMax3.txt,0
		printh 2c
		prints tDelta3.txt,0
		printh 2c
		prints tempDelta3.txt,0
		printh 2c
		prints bStg4.txt,0
		printh 2c
		prints t4.txt,0
		printh 2c
		prints targetTMax4.txt,0
		printh 2c
		prints tDelta4.txt,0
		printh 2c
		prints tempDelta4.txt,0
		printh 2c
		prints bStg5.txt,0
		printh 2c
		prints t5.txt,0
		printh 2c
		prints targetTMax5.txt,0
		printh 2c
		prints tDelta5.txt,0
		printh 2c
		prints tempDelta5.txt,0
		printh 0d 0a

- deleteBtn
	- Explanation: Request program deletion. Only works in edit mode (when s_original_program_name is set). If name mismatch, shows error. Otherwise opens confirmation dialog.
	- Nextion code: Touch Release Event
		prints "delete_prog:",0
		prints progNameInput.txt,0
		printh 0d 0a

- confirmBdy
	- Explanation: Confirmation dialog background. Hidden on page entry. ESP32 shows when delete is requested.
	- Nextion code: None (ESP32 controls visibility)

- confirmTxt
	- Explanation: Confirmation dialog message text (max 40 chars). ESP32 sets content like "Delete \"programname\"?".
	- Nextion code: None (ESP32 sets via confirmTxt.txt="...")

- confirmDelete
	- Explanation: Confirm deletion button. Sends event to ESP32 which performs deletion and hides dialog.
	- Nextion code: Touch Release Event
		prints "confirm_delete",0
		printh 0d 0a

- confirmCancel
	- Explanation: Cancel deletion button. Hides confirmation dialog locally without ESP32 involvement.
	- Nextion code: Touch Release Event
		vis confirmBdy,0
		vis confirmTxt,0
		vis confirmDelete,0
		vis confirmCancel,0

- graphDisp (ID=50)
	- Explanation: Waveform graph for program preview. Hidden on entry; ESP32 shows and renders.
	- Nextion code: None (ESP32 uses vis/cle/add)

- programBuffer
	- Explanation: Serialized program draft buffer (text variable). ESP32 syncs this when editing/switching pages.
	- Nextion code: None

Common 

- Error text display
- errText
	- Explanation: Error body text. Text component shown/hidden by ESP32; content set by ESP32.
	- Nextion code: TODO
- errTxtHead
	- Explanation: Error title/header text. Text component shown/hidden by ESP32.
	- Nextion code: TODO
- errTxtCloseB
	- Explanation: Close button for error overlay. Nextion emits event; ESP32 hides and clears error text.
	- Nextion code: Touch Release Event
		prints "err:close",0
		printh 0d 0a
- Other
- fs0 (filestreamer tool)
	- Explanation: FileStream component used to save program files on Nextion SD. ESP32 calls fs0.open("sd0/<name>.prg"), fs0.val=0, fs0.write(va0.txt,0,<len>), fs0.close().
	- Nextion code: TODO (ESP32 drives methods)
- va0 (variable)
	- Explanation: Global text buffer used as temporary storage for program data before FileStream write. Set .txt_maxl to 4096 (matches PROGRAM_FILE_SIZE).
	- Nextion code: TODO (ESP32 sets va0.txt)