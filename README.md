# AMS2Broker

## What is it?
The AMS2Broker is a tool that enables the automatic configuration of a single race in AUTOMOBILISTA2 (AMS2) from any external application.

## Who needs it and why?
It is intended for modders and application developers who want to create their own championships, special scenarios, and/or complex careers based on AUTOMOBILISTA2.

## How does it work?
The AMS2Broker consists of two parts: a launcher (Launcher.exe) and a DLL (PatcherAPI.dll). The launcher controls the game and injects the DLL. The DLL then acts as a REST API. It receives the configuration and patches the game with the corresponding settings at the click of a button.

## What can’t the AMS2Broker do?
In the current version, it cannot automatically configure multi-class races. If multi-class races are required, the user must configure these settings manually - AFTER he applied the patch!

## What else should you know?
- AMS2 must be launched in “borderless” mode.
- The AMS2Broker is for single-player mode only.
- Settings are patched in the RAM and thus changes aren't directly visible on the screen. You have them when you start the session. 
- After patching, the new settings can be manually overwritten by the user.
- It is the responsibility of the developer that implements the tool to ensure that only vehicles and tracks owned by the user are loaded.
- AMS2 must be started and stopped separately for each race.

## Is it safe to use the AMS2Broker?
I developed this tool in my spare time with no financial interest. That’s why I didn’t spend money on an expensive code-signing certificate. Because of the missing certificate, Windows blocks the tool from running. To run the tool anyway, you’ll once need to click “More info” and then “Run anyway.” 

If that seems too risky: the source code is open. You can examine it and compile the launcher and the DLL yourself. Make sure the names remain exactly the same.

Additionally I’ve used a Random Access Token to prevent third-party software from silently communicating with the REST API. This also prevents malware from being injected from the outside.

So in my opinion, it’s as safe as it can be.

## Which programming languages are supported?
All of them. The race is configured directly through the launcher. Therefore, the application that uses the AMS2Broker can be developed in any language and using any technology.

## Can users create their own races and championships?
Yes, they can, but this requires the developer to implement a feature that loads the additional files into their app.

## In summary:
The AMS2Broker is a tool for automatically configuring a single race. It offers maximum security and convenience for the user and maximum flexibility for the developer.

<hr>

# DEVELOPER GUIDE

### Installation
Download the latest version from [here](https://github.com/dahas/AMS2Broker/releases/latest/download/AMS2Broker.zip) and extract it directly into your app's directory.

### Templates
All races are configured using JSON templates. The template files must be stored in the <code>Path\To\Ams2Broker\Templates</code> directory.

The Templates folder contains 3 examples:  
- full-example.json  
- quick-race-example.json  
- placeholders-example.json  

To familiarize yourself with the *complete configuration* and all parameters, you should start with the **full** example. This allows you to define ALL race settings.

The **quick-race** example omits many parameters. This means you only *override certain settings*, while others remain as the user previously set them. This for example might be a good idea in terms of AI settings.

To avoid having to create separate templates for each championship or series of races, you can use *placeholders*. Take a look at the **placeholders** example for more information.

Here's how to pass your template to the launcher and start the race from your app:

```cs
Path\To\Ams2Broker\Launcher.exe -t "quick-race-example.json"
```

Here’s what it looks like with placeholders:

```cs
Path\To\Ams2Broker\Launcher.exe
  -t "placeholders-example.json"
  -p "#CAR#=-950775810|#LIVERY#=70|#TRACK#=1910889511|#LAPS#=33"
```

Assign individual values to the placeholders for each race. Be sure to separate the individual placeholder/value pairs with the pipe symbol “|” without any spaces or line breaks in between:

```
#PH1#=12345|#PH2#=66|#PH3#=54321|#PH4#=33|...
```

The launcher accepts the following flags:

```
--template / -t
--placeholders / -p
```

### Examples
Here are examples of how to implement the AMS2Broker in different languages. They are not tested, but it should work something like this:

#### JavaScript (Electron/NodeJS):

```php
// Frontend
const { ipcRenderer } = require('electron');

document.getElementById('yourButton').addEventListener('click', () => {
  ipcRenderer.send('start-race');
});
```
```js
// Backend
const { ipcMain } = require('electron');
const { execFile } = require('child_process');

const brokerDir = path.join(__dirname, 'AMS2Broker');
const launcherPath = path.join(brokerDir, 'Launcher.exe');

const templateFile = "placeholders-example.json";

// Get these from a database:
const carId = -950775810;
const liveryId = 70;
const trackId = 1910889511;
const laps = 33;

ipcMain.on('start-race', () => {
  execFile(launcherPath,
    [
      '-t', templateFile,
      '-p', `#CAR#=${carId}|#LIVERY#=${liveryId}|#TRACK#=${trackId}|#LAPS#=${laps}`
    ],
    (err) => {
      if (err) console.error(err);
    });
});
```

#### C#

```cs
using System.Diagnostics;
using System.IO;

string baseDir = AppDomain.CurrentDomain.BaseDirectory;

string pathToLauncher = Path.Combine(baseDir, "Ams2Broker", "Launcher.exe");
string brokerDir = Path.GetDirectoryName(pathToLauncher);

string templateFile = "placeholders-example.json";

// Get these from a database:
long carId = -950775810;
int liveryId = 70;
long trackId = 1910889511;
int laps = 33;

var startInfo = new ProcessStartInfo
{
    FileName = pathToLauncher,
    Arguments = $"-t \"{templateFile}\" -p \"#CAR#={carId}|#LIVERY#={liveryId}|#TRACK#={trackId}|#LAPS#={laps}\"",
    WorkingDirectory = brokerDir,
    UseShellExecute = true
};

Process.Start(startInfo);
```

### Vehicle and Track IDs:

IDs for vehicles and tracks can be retrieved via the AMS2 Dedicated Server. There are several response files in the Resources folder. These may be out of date. To obtain the IDs for the latest content, you can install the Dedicated Server via Steam (Tools) and run it locally. Once the server is running, you can retrieve the information using the following URIs:

http://localhost:9000/api/list/vehicles/  
http://localhost:9000/api/list/vehicle_classes/  
http://localhost:9000/api/list/tracks/  
http://localhost:9000/api/list/liveries/  

### Retrieving race results

To retrieve the race result in your app implement CREST2-AMS2. All information and download links can be found here: 

https://github.com/viper4gh/CREST2-AMS2

# USER GUIDE

As soon as the user clicks the button in your app and goes to the race, the launcher opens and starts the game. The first time this happens, Windows SmartScreen will likely pop up and block the launcher from running. The user must therefore be informed that they need to click “More Information” and then “Run anyway”.

The game launches directly into the single race and applies the patch. If entering the single race fails for some reason and the patch is applied outside of it, the user must enter the single race manually and press the INSERT key on the keyboard to apply the patch again. After the launcher has confirmed the process, they can start the race.

### Troubleshooting:

There is a `config.ini` file in the Ams2Broker directory in which the user can make certain changes if things doesn't work as expected. 

##### Game not found:
The path to AUTOMOBILISTA2 can be entered there if the launcher cannot find the game automatically. 

##### REST-API error:
The user can use a different port if the API that triggers the patching is unresponsive. 

##### Hotkeys already assigned
The hotkeys can be changed as well if their assignments conflict with other apps.

##### Antivirus / Windows Defender False Positive:
Because Launcher.exe uses DLL injection to communicate with AMS2 and is not digitally signed, antivirus software (including Windows Defender) may flag or quarantine Launcher.exe or PatcherAPI.dll as a false positive.

If files disappear or fail to launch:
- Open Windows Security → Virus & threat protection → Protection history and restore the quarantined files.  
- Go to Manage settings → Exclusions and add the entire AMS2Broker folder as an exclusion.  

# CONTRIBUTION

If anyone knows how to implement the multi-classes, I would appreciate your help. I've only managed to get the first 20 or so entries to work reliably. After that, it seems like lazy loading takes place. In total, there could be up to about 110 vehicle classes — depending on which DLCs the user owns. 

I've decided to put the implementation of the multi-classes on hold for now.

# DISCLAIMER

This software allows you to load custom configurations, including vehicles and tracks. The author does not condone piracy or the circumvention of official game licenses. If the software is used in an application that utilizes premium or DLC content, it is solely the responsibility of the mod or app developer to implement appropriate authorization checks and restrict access to users who own the respective content. The author assumes no liability for copyright or license infringements caused by third parties.




