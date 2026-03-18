toolbox
The Page component not listed above is always created when the new Page is added to the Page pane. The page component will always have an .id of 0 and is always the bottommost layer. New page effects and swipe-to-change-page capabilities are added for the Intelligent series. There is a hard limit of 250 components allowed per page.

Many components have multiple .sta choices of
– crop image (pulls background from .x and .y location of a user chosen picture resource)
– solid color (sets background to be a user chosen 565 color)
– image (text if any will be drawn over the user chosen image)
– Intelligent / Edge transparency (pulls background from underlying layers for transparent pixels)

Many components have multiple .style choices of
– flat (no lines will be added around edges)
– border (lines will be drawn around edges)
– 3D_Down (lines will be drawn to yield impression of lowered)
– 3D_Up (lines will be drawn to yield impression of raised)
– 3D_Auto (lines will be drawn Up/Down according to state)

Many components have multiple text alignment and placement options
– .xcen of Left, Center or Right
– .ycen of Top, Center or Bottom
– .spax will add extra blank pixel spacing to right of each character
– .spay will add extra blank pixel spacing to bottom of each character
– .isbr for multi-lined (set to true) or single line (set to false)
– .pw for masking (Character is off, Password will mask with an asterisk)

Intelligent / EdgeFor the Intelligent and Edge Series, most components have new capabilities

repositionable by assigning .x and .y values, or by move instruction
user draggable at runtime by setting .drag
alpha channel blending (fading effect) by setting .aph attribute
additional attributes not available for the Basic, Discovery or Enhanced component
and other effects found in the .effect attribute
NOTE: with many more customizable attributes for the Intelligent Series components, it is wise to click the attribute in the Attribute Pane (see Section 2.6) for a full attribute description. ie: Intelligent Series Gauge .vvs2 attribute now controls the foot length of the gauge needle (attribute not available for the Basic, Discovery or Enhanced Series).
The Text component is a highly customizable component. The Text component has the .pw attribute for masking (Character is off, Password will mask with asterisk) and the .key attribute for integrating one of the included example keyboards (must be set to .vscope global before use).

The Scrolling text component combines an integrated timer component with a text component. The .pw option is not available with this component. The .key attribute allows for integrating one of the included example keyboards (must be set to .vscope global before use). There is a hard limit of 12 timer components per page within your project.

The Number component is used for signed 32-bit integer values. The .lenth (as spelled) sets the number of digits shown (useful for leading zeros). The .format attribute allows for a choice of integer, currency (comma separated every three digits, not float values), or hexadecimal. Input should be in integer or hexadecimal. The .key attribute allows for integrating one of the included example keyboards (must be set to .vscope global before use).

The Xfloat component is used for signed 32-bit integer values. The .vvs0 sets the number of digits shown to the left of the decimal (useful for leading zeros). The .vvs1 sets the number of digits shown to the right of the decimal. The .key attribute allows for integrating one of the included example keyboards (must be set to .vscope global before use).

The Button component is again highly customizable and integrates text in a momentary manner. Use text, images and event code to suit tastes.

The Progress bar component is for progress, thus a valid range of 0 to 100 to represent the percentage of progress. (Please no more requests to extend the range, even if many may give 110% effort). Best effects for progress are attained using images.

The Picture component will allow any picture resource to display in the Picture component. Example p0.pic=3. It is important that the picture resource matches the user defined size in .w and .h or the picture resource will over draw the picture component boundaries, or incorrectly insert adjoining data. The Picture component is useful to represent multi-states and animation sequences. Note: when you want a background picture to a Page, do not create a full screen sized Picture component over top the .sta solid color page: This will likely result in flickering on redrawing. Rather, set the page to .sta image and set the now exposed .pic attribute to the desired Picture Resource image. In this manner you achieve a page background image without the background being the cause of flicker.

The Crop component will replace its boundaries with the same location and boundaries from the picture resource pointed to with .picc. It is highly recommended that the picture resource being used is a full screen image to avoid errors (must be fullscreen image). The Crop component is useful to represent states.

The Hotspot component is a user defined touch spot to its overlaying region. At a 2 pixel by 2 pixel region, it makes for a useful code holder to be later called by the click instruction – thereby creating a user defined function. As a Hotspot, it turns any image area beneath into a button, such as in creating a customized keyboard.

The TouchCap component is a non-visual component that stores in its .val attribute the .id value of the last component touched (pressed or released). Even though the TouchCap is a non-visual component it has both Touch Pressed and Touch Released events that are triggered on the internal setting of the .val value, this is useful to trigger code in the TouchCap events on a touch event (pressed or released) as would be the case in any other visual component. Only one TouchCap component will be useful on a page – if there are two or more TouchCap components on a page, the TouchCap component with the highest .id value will supercede any TouchCap component with a lower .id value. Note: A TouchCap Send Component ID checkbox will always reset to the unchecked state as it is never this component that triggered the physical touch. Rather, use the individual component’s Send Component ID checkboxes or mimic a touch event using print statements. Touch Event format is listed in NIS 7.21
(ie: printh 65 prints dp,1 prints tc0.val,1 printh 00 or print 01 and printh FF FF FF).

It is also note worthy to mention as a reminder: Nextion Resistive devices have only process a single touch – meaning first component pressed will be registered and no other component can register until the first component pressed is released. Nextion Capacitive devices are multi-touch devices registering up to 5 different touch components and they are registered in the order of pressed and released as they happen in sequence they occur up until when the 6th component is pressed and the sixth component will not register. This complexity is simply the nature of multi-touch and requires a higher degree of workflow planning for precision execution – Nextion still remains consecutive processing of events and a “next-event” will not be processed until the current event has completed.

The Gauge component is a full circular component with value in degrees. This means a range of 0 to 360. In Basic, Discovery and Enhanced series: Gauge components are not useful for stacking (example: a three handed clock), as the redrawn gauge will overwrite any lower gauge. The gauge component is always a square in nature. Semi-circular gauges at the screen’s edge are not achieved with the gauge component. In the Intelligent series: a stackable gauge with highly customizable needles and out of bounds positioning can achieve many effects not possible in the other series.

The Waveform component is used to plot y axis data points on up to 4 channels. Waveforms are never global: in that adding datapoints with add/addt can only be done when waveform is on the active current page. Waveform .vscope of global allows plotted data to be maintained between page changes. Waveform .vscope of local, the data points are not retained, changing pages away and back will revert the waveform to an emptied state. Up to 4 waveforms can be used on a single page. The Waveform component is limited to a y axis data range of 0 to 255 or 0 to waveform height -1. As a data point is added, it will consume one column, with the next data point using the next column. Recent changes now allow a variable to be used in the add command. Example add 1,0,h0.val. The addt command becomes useful to refill the waveforms on page load (such coding remains within the user domain).

The Slider component can be horizontal or vertical. The slider has the added event code for Touch Move, useful for providing updates to the sliders current position. Best results are attained with images. Slider length includes the size of the thumb as well as the range (often overlooked in calculations). Snapping a slider to its value position can be achieved with h0.val=h0.val where slider .objname is h0.

The Timer component is not expected to be a high precision interrupt driven component. It is however useful for queueing reoccurring event code after elapsed .tim has expired. As code is sequentially processed, it is very easy for the time to process the requested user event code to exceed the .tim intervals and therefore not interrupt driven (to avoid such stack overflows) and not high precision. There is a hard limit on the maximum number of timers running in a single page, this limit is 12. Beware that the scrolling text component integrates 1 timer. Timer attributes can have a variable scope of global, event code is never global. As such timer code can only be triggered within the current page they are designed in. As the timer is a non visual component, they are added below the Design Canvas.

The Variable component is a non visual component and also added below the Design Canvas. Variables are either 32 bit signed numeric or string content can be selected with the .sta attribute at design time.

The Dual-state button component is an expanded Button maintaining its toggling state between press/releases.

The Checkbox component is another example of a lightweight dual-state component with less customization and lower memory usage.

The Radio component is yet another example of a lightweight dual-state component with little customization and lower memory usage. Obtaining grouping is achieved via user code (remains in the user domain).

The QRcode component is used to generate a 2D scanable QR. It is limited to a byte maximum for the .txt_maxl attribute of 84 (of up to a max 154 bytes) on Basic T models and 192 bytes on the Enhanced K and Intelligent P models. The QR component will consume some user HMI SRAM when included in an HMI project to facilitate the QR rendering.

New components for the Intelligent / Edge Intelligent and Edge series

The Switch component is an expanded dual-state combining text and graphics.

The ComboBox component is used to present an expandable/collapsible selectable list with the .path attribute holding options one per line.  The number of selected item held in .val and .txt holding the text of the selected item.

The TextSelect component is used to present a cyclic spinner with .path attribute holding options one per line. The selected item number in .val and .txt holding the read-only text of the selected item.

The SLText component is used to present a scrollable textbox with .txt holding multiline data. The position of the list in pixels can be set through the .val_y attribute.

The DataRecord component is used to present a dataset in a scrollable table.  DataRecord supports up to 12 fields per record.   DataRecord incorporates 4 methods .insert(), .delete(), .up() and .clear().  Configuring DataRecord attributes for your application requirements will need be thoughtful at your HMI design time.

The FileBrowser component is used to present a folder and file structure tiled in a filter capable browser.  The .dir attribute holds the folder path, and the .txt attribute holds the selected filename.  FileBrowser incorporates an .up() method to return to previous folder. Be sure to ensure an appropriate font has been included to your project for filenames to be displayed.

The FileStream component is a non visual component for working with a file, it incorporates 5 methods .open(), .close(), .read(), .write() and .find().  A file is of finite size when user creates the file, appending beyond the file end is not supported.

The Gmov component is used to present an animation, with up to 16 Gmov components on an HMI page.  Use the GMovMaker Tool to create an animation in the Nextion *.gmov format using supported *.jpg, *.bmp, *.png and *.gif source files.  The Gmov component contains an additional Play completed Event to trigger user code at the end of each iteration of the animation.

The Video component is used to present a movie, with up to 6 Video components on an HMI page.  Use the VideoBox Tool to convert a movie into the Nextion *.video format. The Video component contains an additional Play completed Event to trigger user code at the end of each iteration of the Video. Use the .from attribute as external file and .path attribute to use *.video files stored in ram or on Nextion’s microSD card.

The Audio component is used to present wav files that are stored in ram or microSD card.  Use the VideoBox Tool Audio tab to create audio resources in the Nextion *.wav format. The Audio component contains an additional Play completed Event to trigger user code at the end of each iteration of the Audio. Use the .from attribute as external file and .path attribute to use *.wav files stored in ram or on Nextion’s microSD card. Note: Nextion Audio capabilities is 2 Channel with 10 band equalizer with output to a mono speaker. See Instruction Set play instruction, and system variables volume, audio0/audio1, eql/eqm/eqh, and eq0..eq9 in NIS Section 6.

The ExPicture component is used to present pictures that are stored either in ram or on microSD card.  Use the PictureBox Tool to create picture resources in the Nextion *.xi format that can be stored in ram or on Nextion’s microSD card. ExPicture can display images that are in the same selected orientation as the HMI and either Nextion’s *.xi format or JPEG Baseline DCT only.

To add any of the above components to the currently design page, simply click on the component and it will be added with its .id set to the number of components on the page. All components within a page are listed in .id ascending order in the Component Drop down in the Attributes Pane. Then continue with placement and adjustment of .attributes as desired.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
attribute pane
The Attribute Pane contains the list of components included within the current design page in the Component drop down. Clicking on a component, or selecting it from the drop down will display the component’s available attributes. The left side contains the attribute name, the right side contains the attributes current value. Clicking on an attribute will display the attributes meaning and valid range/options at the bottom of the Attribute Pane. Double clicking a field with bring up resource editor for the attribute if attribute has such (ie: .pco opens color picker, .pic opens picture chooser).

Any attribute in black is read only at runtime (with the exception of .objname which remains inaccessible). Any attribute in green can be both read and changed by user code at runtime. Empty unassigned attributes values or invalid attribute values will need to be resolved before a successful compile can be achieved. When renaming a Components .objname: avoid using space and other characters that can be code ambiguous as this could cause code parsing issues.

Page name prefixing is suggested to access a global component’s attributes on another page.
– example: page0.va0.val
Page name prefixing is not required to access a local component’s attribute on the current page
– example: va0.val

Attributes that have ranges are evaluated in full during Nextion’s parsing of a complex expression and as such care is required. Nextion is stated as simplex expression, although these rules are often bent. Use care.
– example: gauge z0 with a .val range of 0 to 360.
z0.val=va0.val+step.val%360
Should va0.val+step.val exceed 360 the assignment fails before arriving at modulo 360.
– this is the nature of simplex expressions (think assembly language), and bending the rules has side effects.

The various combinations of attribute choices provides a wide range of expected behaviours with too many combinations to cover in any manual(s). This combined with the Nextion Instruction Set creates the opportunity for very powerful HMIs.

There is an HMI hard limit for a combined tally of attributes and user code of 65534.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Non Visual Components
A Page’s non visual components (Variable, TouchCap, Timer, Audio and FileStream) will be listed in the area under the Design Canvas. This area is not displayed if no non-visual components are used in the page. There is a limit of 250 components (visual and non visual) allowed per page.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
User Event Code
User Event Code can contain any valid Nextion Instruction. This section will not teach programming, but will quickly give an overview of the various types of Events where inserting user code is available. Event code is always local to page and never global.

Almost every component has the Touch Press and Touch Release events.
– The Touch Press Event includes a Send Component ID checkbox that when checked sends the 0x65 Return Data over serial on physical press. User code is run on either physical press or via the click command.
– The Touch Release Event includes a Send Component ID checkbox that when checked sends the 0x65 Return Data over serial on physical release. User code is run on either physical release or via the click command.
– The Send Component ID 0x65 Return Data over serial action can not be triggered by the click command. This is reserved for an end-user physical action received through the touch sensor. The click command will only trigger running the user event code.
– The Nextion Instruction printh command can simulate 0x65 Return Data (Actual or spoofed)

The Page component contains both Touch Press and Touch Release as well as
– The Preinitialize Event user code is run before the loading of the HMI designed page.
– The Postinitalize Event user code is run after the loading of the HMI designed page.
– The Page Exit Event user code is run as last event before a Page change
Note: .sta global values will persist, while .sta local values return to their designed state.
Note: Page component Pre and Post initialize Events are not triggered on exiting sleep, rather the components are visually refreshed to render the visual screen space to its wake state. Pre and Post Initialize events are triggered when entering a page. If page initialize events are required when exiting sleep, consider using the purposefully designed wup system variable.

The Slider component contains Touch Press and Touch Release as well as Touch Move.
– The Touch Move Event user code is run during drag of thumb when slider changes values.
– The Touch Move Event does not have a Send Component ID to avoid serial overflow.

The Timer component only contains the Timer Event for user code.
– Refer to the Timer component in the Component Pane section for more details

The Gmov, Audio and Video components contain the additional Play Completed Event.
– the Play Completed Event is run after each iteration of the sequence completes

Nextion Return Data is returned after the end of command execution
– it would otherwise not be wise to predict an outcome before the end.

There is an HMI hard limit for a combined tally of attributes and user code of 65534.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Output
The Output Pane contains details on the build process when Compile/Debug/Upload is selected. Compile needing to occur first, the user HMI is assembled into a usable TFT file for the selected Nextion Model. The first four lines of the output will list the total amount of Available Memory, Global SRAM Memory consumed by the HMI project, and then statistics for the total amount of Flash space the picture resources consumes, followed by the total amount of Flash space the ZI Font resources consumes. For the Intelligent series: a line will be displayed for each Gmov, Video and Audio with the total amount of Flash space each resource group consumes.

The build process then goes through the project sequentially page by page. At the end of a successful page build, the page Memory stats are listed Global+Local=Total. Should a page not build successfully, the offending page is the last listed+1.
Warnings listed in blue (such as when using the not recommended layering techniques, it will compile, but warn of potential unexpected behaviours), Errors listed in red (this will not compile, and the build process halts). Note: Do not upload a zero byte *.tft file.

File Size must be small enough to fit in your Model’s Flash size. See the Status Bar or your Datasheet for your model’s Flash size. All pages’ Total Memory usage must be small enough to fit your model’s HMI allotted SRAM. See the Status Bar or your Datasheet for your model’s HMI allotted SRAM.

Due to the nature of flash, it is possible that a compiled filesize may be under the MB size (ie: 1677216 bytes) and still be shy of the available and usable Flash on the Nextion Device. Nextion may report on upload the File is too big – this is not a hardware error but the working nature of Flash. Some allowance for wear-leveling and unusable flash pages have to be made, and even more so over time.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Display / Program.s Tabs
Use the Display Tab to show the Design Canvas
Use the Program.s Tab to set project global variables and project start up code. Variable declarations are made first, followed by code, and finally the power on start page. Code after a page change to the start page can never be executed as the start page will never return back to the start to complete the section after page change line. As of v1.65.0, the Nextion Preamble (NIS 7.19 and NIS 7.29) have been moved from firmware to the Program.s tab as a printh statement allowing the user to have choice if this startup notification is given or perhaps user customized. It is recommended to set baud, dim and recmod in Program.s rather than in Page Preinitialize events.
------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
