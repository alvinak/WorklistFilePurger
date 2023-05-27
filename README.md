### Modality Worklist Purger plugins for Orthanc DICOM Server
An Orthanc Plugin for clearing the Modality Worklist files, once the instance received in Orthanc PACS Server

#### OverView
When an Instance with a matching accession number or Study Instance UID received in Orthanc PACS, this plugin will remove the corresponding worklist entry from the worklist database folder. 
The plugin reads the worklist database path from the "ModalityWorklists" section of the Orthanc Configuration, therefore it is mandatory to have this section available in the Orthanc Configuration file for the smooth working of the plugin.

This plugin also provides option to "Enable" or "Disable" this functionality using the below interface.
```bash
/enableWorklistPurge     ==> Enable/On the Plugin functionality             // http://localhost:8042/enableWorklistPurge
/disableWorklistPurge    ==> Disable/Off the Plugin functionality           // http://localhost:8042/disableWorklistPurge
/worklistPurgeStatus     ==> Show the current status Enabled/Disabled       // http://localhost:8042/worklistPurgeStatus
```

#### Building the Plugin
Check out the code and copy it into the Orthanc Samples folder
```bash
cd OrthancServer/Plugins/Samples
git clone https://github.com/alvinak/WorklistFilePurger.git
cd WorklistFilePurger
mkdir Build  
cd Build   
cmake -DSTATIC_BUILD=ON -DCMAKE_BUILD_TYPE=Release ../   
make  
```

#### Callback functions used
```bash
OrthancPluginRegisterOnStoredInstanceCallback
OrthancPluginRegisterRestCallback
```

#### Side note 1
In a study, there can be many instances and it is not required to process all these instances other than one of the instances of the study.
To avoid this scenario, the plugin creates a .json file (one .json file for every day) in the Orthanc root folder, and the entries of this file are the accession number and Study Instance UID of the deleted worklist files. In the StoredInstanceCallback function, there is an initial checking happening for every instance to proceed further, based on this .json file.

#### Side note 2
The starting point of this plugin functionality is the StoredInstanceCallback function.

More ideal way is to use Modality Peroformed Procedure Step (MPPS) and it can be achieved by using RegisterOnChangeCallback function as per the below Orthanc group discussion.
https://groups.google.com/g/orthanc-users/c/Tec0xgD4s2c
There is a plan to implement RegisterOnChangeCallback in the future release of this plugin.


