# USER GUIDE

After you have clicked the button "Go to race", the launcher opens and starts the game. The first time this happens, Windows SmartScreen will likely pop up and block the launcher from running. You must click “More Information” and then “Run anyway”.

Once the game reaches the main menu, enter the single race. There, press the INSERT key on the keyboard, which patches the configuration. After confirming the process in the launcher, you can start the session.

### Troubleshooting:

There is a `config.ini` file in the Ams2Broker directory in which you can make certain changes if things doesn't work as expected. 

##### Game not found:
The path to Automobilista 2 can be entered there if the launcher cannot find the game automatically. 

##### REST-API error:
You can use a different port if the API that triggers the patching is unresponsive. 

##### Hotkeys already assigned
The hotkeys can be changed as well if their assignments conflict with other apps.

##### Antivirus / Windows Defender False Positive:
Because Launcher.exe uses DLL injection to communicate with AMS2 and is not digitally signed, antivirus software (including Windows Defender) may flag or quarantine Launcher.exe or PatcherAPI.dll as a false positive.

If files disappear or fail to launch:
- Open Windows Security → Virus & threat protection → Protection history and restore the quarantined files.  
- Go to Manage settings → Exclusions and add the entire AMS2Broker folder as an exclusion.  

