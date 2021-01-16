/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2020 Osimis S.A., Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/




#include <orthanc/OrthancCPlugin.h>
#include "../Common/OrthancPluginCppWrapper.h"


#include <boost/filesystem.hpp>
#include <json/value.h>
#include <json/reader.h>
#include <string.h>
#include <iostream>
#include <algorithm>

#define DCM_TAG_STDIUID "0020,000d"
#define DCM_TAG_ACCNO "0008,0050"
#define WORKLIST_PURGE_CACHE "WorklistPurgeCache"
static std::string folder_;

bool worklistPurgerRESTStatus;


static void CacheTheDetailsToLocalFile(const char* incomingFileStdIUID, const char* inComingFileAccessionNumber){
  time_t t = time(0);   // get time now
  struct tm * now = localtime( & t );

  char pathBuffer [128] = {0};
  strftime (pathBuffer, 128, "WorklistPurgeCache_%Y-%m-%d.json", now);

  std::fstream jfile;
  jfile.open (pathBuffer, std::ios::in);

  Json::Reader reader;
  Json::Value json_obj;

  if(!reader.parse(jfile, json_obj, true))
  {
      // json file must contain an array
      std::cerr << "could not parse the json file" << std::endl;
      // return;
  }

  jfile.close();

  Json::Value m_event;
  m_event["study"]["accesionNo"] = inComingFileAccessionNumber;
  m_event["study"]["studyIUID"] = incomingFileStdIUID;//msg;

  // append to json object
  json_obj.append(m_event);

  std::cout << json_obj.toStyledString() << std::endl;

  // write updated json object to file
  jfile.open(pathBuffer, std::ios::out);
  jfile << json_obj.toStyledString() << std::endl;
  jfile.close();
}

//Checking for already processed DICOM instance. 
bool IsThisStudyAlreadyProcessed(const char* incomingFileStdIUID, const char* inComingFileAccessionNumber){

  bool bFoundAlreadyProcessedEntry = false;
  time_t t = time(0);   // get time now
  struct tm * now = localtime( & t );

  char pathBuffer [128] = {0};
  strftime (pathBuffer, 128, "WorklistPurgeCache_%Y-%m-%d.json", now);

  std::fstream jfile;
  jfile.open (pathBuffer, std::ios::in);

  Json::Reader reader;
  Json::Value json_obj;

  if(!reader.parse(jfile, json_obj, true))
  {
      // json file must contain an array
      std::cerr << "could not parse the json file" << std::endl;
      return bFoundAlreadyProcessedEntry;
  }

  jfile.close();

  for (int i = 0; i < json_obj.size(); i++){
      
      const char* studyIUIDCached = json_obj[i]["study"]["studyIUID"].asString().c_str();
      const char* accNoCached = json_obj[i]["study"]["accesionNo"].asString().c_str();

      
      if(0 == strcmp(studyIUIDCached, incomingFileStdIUID) || 
          0 == strcmp(accNoCached, inComingFileAccessionNumber)){
              bFoundAlreadyProcessedEntry = true;

              break;
          }
  }

  return bFoundAlreadyProcessedEntry;
}


// Getting the tags of the created instance is done by a quick-and-dirty parsing of a JSON string.
static void getDicomTag(char* json, char* tag, char* tagValue, unsigned int len)
{
	char *jsonText = (char *)malloc((strlen(json) + 1) * sizeof(char));
	strcpy(jsonText, json);

	if (strlen(tag) < 100)
	{
		char tagText[100] = "\"";
		strcat(tagText, tag);
		strcat(tagText, "\" : \"");

		char* value = strstr(jsonText, tagText);
		int offset = strlen(tagText);
		value = value + offset;
		char* eos = strchr(value, '\"');
		eos[0] = '\0';
		//sprintf(buffer, "value[%d]=%s", strlen(value), value);
		//OrthancPluginLogWarning(context, buffer);

		if (strlen(value) < len-1)
			strcpy(tagValue, value);
		else
			strcpy(tagValue, "TAG ERROR");
	}
	else
		strcpy(tagValue, "VALUE ERROR");

	free(jsonText);
}

static bool ReadAndVerifyWorklistFile(std::string& result,
                     const std::string& worklistPath, const char* incomingFileStdIUID, const char* inComingFileAccessionNumber)
{
  
  OrthancPlugins::MemoryBuffer dicom;
  try{
    OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();
    dicom.ReadFile(worklistPath);

    // Convert the DICOM as JSON, and dump it to the user in "--verbose" mode
    Json::Value json;
    dicom.DicomToJson(json, OrthancPluginDicomToJsonFormat_Short,
                      static_cast<OrthancPluginDicomToJsonFlags>(0), 0);

    char workliststdIUID[64] = {0};
    char worklistAccNo[64] = {0};

    bool bFoundStdIUID = false, bFoundAccessionNo = false;
    
    for (Json::Value::const_iterator it=json.begin(); it!=json.end(); ++it)
    {
      const char *dcmTag =  it.key().asString().c_str();
      if(0 == strcmp(DCM_TAG_STDIUID, dcmTag)){
        strcpy(workliststdIUID, it->asString().c_str());
        bFoundStdIUID = true;
      }

      if(0 == strcmp(DCM_TAG_ACCNO, dcmTag)){
        OrthancPlugins::LogWarning("Getting Accession No");
        OrthancPlugins::LogWarning(it->asString().c_str());
        strcpy(worklistAccNo, it->asString().c_str());
        bFoundAccessionNo = true;
      }

      if(bFoundStdIUID){
        break;
      }

    }

    if(!bFoundAccessionNo && !bFoundStdIUID){
        OrthancPluginLogInfo(context, "StudyIUID and AccessionNo are empty in the Worklist file, hence returning");
        return false;
    }
      

    char logBuffer[256] = {0};

    if(0 == strcmp(workliststdIUID, incomingFileStdIUID) || 0 == strcmp(worklistAccNo, inComingFileAccessionNumber)){
      sprintf(logBuffer, "Found matching worklist");
      OrthancPluginLogInfo(context, logBuffer);

      memset(logBuffer, 0, sizeof(logBuffer));

      sprintf(logBuffer, "Std IUID from Worklist file=%s\tStd IUID from incoming DICOM file=%s\nAccession No from Worklist=%s\tAccession No from incoming DICOM file=%s", 
                  workliststdIUID, incomingFileStdIUID, worklistAccNo, inComingFileAccessionNumber);

      OrthancPluginLogInfo(context, logBuffer);
      CacheTheDetailsToLocalFile(incomingFileStdIUID, inComingFileAccessionNumber);
      return true;
    }

    return false;
  }
  catch (...)
  {
    return false;
  }
 
}




ORTHANC_PLUGINS_API OrthancPluginErrorCode EnableWorklistPurger(OrthancPluginRestOutput* output,
  const char* url, const OrthancPluginHttpRequest* request) {
    worklistPurgerRESTStatus = true;
  std::string HtmlCode = "<html>\n<head>\n<title>Plugin1</title>\n</head>\n<body>Worklist Purger enabled</body>\n</html>\n";
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, HtmlCode.c_str(), HtmlCode.length(), "text/html");
  return OrthancPluginErrorCode_Success;
}

ORTHANC_PLUGINS_API OrthancPluginErrorCode DisableWorklistPurger(OrthancPluginRestOutput* output,
  const char* url, const OrthancPluginHttpRequest* request) {
    worklistPurgerRESTStatus = false;
  std::string HtmlCode = "<html>\n<head>\n<title>Plugin1</title>\n</head>\n<body>Worklist Purger disabled</body>\n</html>\n";
  OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, HtmlCode.c_str(), HtmlCode.length(), "text/html");
  return OrthancPluginErrorCode_Success;
}

ORTHANC_PLUGINS_API OrthancPluginErrorCode WorklistPurgerStatus(OrthancPluginRestOutput* output,
  const char* url, const OrthancPluginHttpRequest* request) {
    if(worklistPurgerRESTStatus){
      std::string HtmlCode = "<html>\n<head>\n<title>Plugin1</title>\n</head>\n<body>Worklist Purger Status is: Enabled</body>\n</html>\n";
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, HtmlCode.c_str(), HtmlCode.length(), "text/html");
    }
    else
    {
      std::string HtmlCode = "<html>\n<head>\n<title>Plugin1</title>\n</head>\n<body>Worklist Purger Status is: Disabled</body>\n</html>\n";
      OrthancPluginAnswerBuffer(OrthancPlugins::GetGlobalContext(), output, HtmlCode.c_str(), HtmlCode.length(), "text/html");
    }
  
  return OrthancPluginErrorCode_Success;
}

OrthancPluginErrorCode OnChangeCallback(OrthancPluginChangeType changeType,
                                             OrthancPluginResourceType resourceType,
                                             const char* resourceId)
{
  char info[1024];
  OrthancPluginMemoryBuffer tmp;

  sprintf(info, "Change %d on resource %s of type %d", changeType, resourceId, resourceType);
  OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), info);

  if (changeType == OrthancPluginChangeType_NewInstance)
  {
    // sprintf(info, "/instances/%s/metadata/AnonymizedFrom", resourceId);
    sprintf(info, "/instances/%s/metadata?expand", resourceId);
    if (OrthancPluginRestApiGet(OrthancPlugins::GetGlobalContext(), &tmp, info) == 0)
    {
      sprintf(info, "\n*****  Instance %s comes from OnChangeCallback", resourceId);
      strncat(info, (const char*) tmp.data, tmp.size);
      OrthancPluginLogWarning(OrthancPlugins::GetGlobalContext(), info);
      OrthancPluginFreeMemoryBuffer(OrthancPlugins::GetGlobalContext(), &tmp);
    }
  }

  return OrthancPluginErrorCode_Success;
}

OrthancPluginErrorCode OnStoredCallback(const OrthancPluginDicomInstance* instance,
                                        const char* instanceId)
{
  OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();
  char buffer[1024] = {0};
  sprintf(buffer, "Just received a DICOM instance of size %d and ID %s from origin %d (AET %s)", 
          (int) OrthancPluginGetInstanceSize(context, instance), instanceId, 
          OrthancPluginGetInstanceOrigin(context, instance),
          OrthancPluginGetInstanceRemoteAet(context, instance));
  OrthancPluginLogInfo(context, buffer);

  if(!worklistPurgerRESTStatus){
    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "\n******    WorklistFilesPurger Plugin disabled ******\n");
    OrthancPluginLogInfo(context, buffer);
    return OrthancPluginErrorCode_Success;
  }
  

  char studyInstanceUIDStr[64] = {0}; // Max length of UID can be 64
  char accessionNumber[64] = {0};
  char buffer1[512] = {0};

  char* jsonIncomingDICOM;
  jsonIncomingDICOM = OrthancPluginGetInstanceSimplifiedJson(context, instance);

  // strcpy(studyInstanceUIDStr, "1.2.276.0.7230010.3.2.101");
  getDicomTag(jsonIncomingDICOM, "StudyInstanceUID", studyInstanceUIDStr, 64);  
  getDicomTag(jsonIncomingDICOM, "AccessionNumber", accessionNumber, 64);  

  sprintf(buffer1, "\n******\n\n    StudyInstanceUID=%s\n    AccessionNumber=%s\n*********\n", studyInstanceUIDStr, accessionNumber);
  OrthancPluginLogInfo(context, buffer1);

  if(strlen(studyInstanceUIDStr) == 0 && strlen(accessionNumber) == 0){

    OrthancPlugins::LogWarning("Both Study IUID and Accesssion Number are empty in the received instance, hence returning");
    return OrthancPluginErrorCode_Success;
  }


  // Checking for already processed Study
  if(IsThisStudyAlreadyProcessed(studyInstanceUIDStr, accessionNumber)){

    OrthancPluginLogInfo(context, "Worklist file purging already done for this instance");
    return OrthancPluginErrorCode_Success;
  }

  //Loop over the regular files in the database folder
  namespace fs = boost::filesystem;

  fs::path source(folder_);
  fs::directory_iterator end;

  try
  {
    for (fs::directory_iterator it(source); it != end; ++it)
    {
      fs::file_type type(it->status().type());

      if (type == fs::regular_file ||
            type == fs::reparse_file)   // cf. BitBucket issue #11
      {
          std::string extension = fs::extension(it->path());
          std::transform(extension.begin(), extension.end(), extension.begin(), tolower);  // Convert to lowercase

          if (extension == ".wl")
          {

            const char* filePath = it->path().string().c_str();
            memset(buffer1, 0, sizeof(0));
            sprintf(buffer1, "\n******    Worklist File Path =%s ******\n", filePath);

            OrthancPluginLogInfo(context, buffer1);

            
            std::string result;
            bool foundMatchingWorklist = ReadAndVerifyWorklistFile(result, filePath, studyInstanceUIDStr, accessionNumber);

            if(foundMatchingWorklist){

              memset(buffer1, 0, sizeof(buffer1));
              sprintf(buffer1, "\n******    Removing the worklist file %s ******", filePath);
              OrthancPluginLogInfo(context, buffer1);
              
              remove(filePath);

              break;

            }  

          }
      }
    }
  }
  catch (fs::filesystem_error&)
  {
    OrthancPlugins::LogError("Inexistent folder while scanning for worklists: " + source.string());
    return OrthancPluginErrorCode_DirectoryExpected;
  }

  return OrthancPluginErrorCode_Success;
}


extern "C"
{
  ORTHANC_PLUGINS_API int32_t OrthancPluginInitialize(OrthancPluginContext* c)
  {
    OrthancPlugins::SetGlobalContext(c);

    /* Check the version of the Orthanc core */
    if (OrthancPluginCheckVersion(c) == 0)
    {
      char info[1024];
      sprintf(info, "Your version of Orthanc (%s) must be above %d.%d.%d to run this plugin",
              OrthancPlugins::GetGlobalContext()->orthancVersion,
              ORTHANC_PLUGINS_MINIMAL_MAJOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_MINOR_NUMBER,
              ORTHANC_PLUGINS_MINIMAL_REVISION_NUMBER);
      OrthancPluginLogError(OrthancPlugins::GetGlobalContext(), info);
      return -1;
    }

    worklistPurgerRESTStatus = true;

    OrthancPlugins::LogWarning("WorklistFilePurger :  plugin is initializing");
    OrthancPluginSetDescription(c, "Delete worklist file after receiving a matching instance in Orthanc PACS");


    OrthancPlugins::OrthancConfiguration configuration;

    OrthancPlugins::OrthancConfiguration worklistsConf;
    configuration.GetSection(worklistsConf, "Worklists");


    bool enabled = worklistsConf.GetBooleanValue("Enable", false);

    if (enabled)
    {
      if (worklistsConf.LookupStringValue(folder_, "Database"))
      {
        //Registering Callbacks

        OrthancPluginRegisterOnStoredInstanceCallback(OrthancPlugins::GetGlobalContext(), OnStoredCallback);
        OrthancPluginRegisterRestCallback(OrthancPlugins::GetGlobalContext(), "/enableWorklistPurge", EnableWorklistPurger);
        OrthancPluginRegisterRestCallback(OrthancPlugins::GetGlobalContext(), "/disableWorklistPurge", DisableWorklistPurger);
        OrthancPluginRegisterRestCallback(OrthancPlugins::GetGlobalContext(), "/worklistPurgeStatus", WorklistPurgerStatus);
	      
	// OrthancPluginRegisterOnChangeCallback(OrthancPlugins::GetGlobalContext(), OnChangeCallback);


      }
      else
      {
        OrthancPlugins::LogError("WorklistFilePurger : The configuration option \"Worklists.Database\" must contain a path in the ORthanc configuration file");
        return -1;
      }

    }
    else
    {
      OrthancPlugins::LogWarning("WorklistFilePurger : Worklist server is disabled in the ORthanc configuration file");
    }

    return 0;

  }


  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    OrthancPlugins::LogWarning("WorklistFilePurger plugin is finalizing");
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return "worklist-file-purger";
  }
	


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return WORKLIST_FILE_PURGER_VERSION;
  }

  

}
