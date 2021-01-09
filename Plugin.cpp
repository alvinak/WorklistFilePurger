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
// #include "../../../../OrthancFramework/Sources/Compatibility.h"
#include "../Common/OrthancPluginCppWrapper.h"


#include <boost/filesystem.hpp>
#include <json/value.h>
#include <json/reader.h>
#include <string.h>
#include <iostream>
#include <algorithm>

#define DCM_TAG_STDIUID "0020,000d"
static std::string folder_;



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

		//char buffer[10000];
		//sprintf(buffer, "tagText[%d]=%s", strlen(tagText), tagText);
		//OrthancPluginLogWarning(context, buffer);

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
                     const std::string& path, const char* incomingFileStdIUID)
{
  
  
  

  OrthancPlugins::MemoryBuffer dicom;
  try{
    OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();
    dicom.ReadFile(path);

    // Convert the DICOM as JSON, and dump it to the user in "--verbose" mode
    Json::Value json;
    dicom.DicomToJson(json, OrthancPluginDicomToJsonFormat_Short,
                      static_cast<OrthancPluginDicomToJsonFlags>(0), 0);

    char workliststdIUID[64] = {0};
    
    for (Json::Value::const_iterator it=json.begin(); it!=json.end(); ++it)
    {
      const char *dcmTag =  it.key().asString().c_str();
      if(0 == strcmp(DCM_TAG_STDIUID, dcmTag)){
        strcpy(workliststdIUID, it->asString().c_str());
        break;
      }
    }

    char logBuffer[64] = {0};

    if(0 == strcmp(workliststdIUID, incomingFileStdIUID)){
      sprintf(logBuffer, "Found matching worklist");
      OrthancPluginLogInfo(context, logBuffer);

      memset(logBuffer, 0, sizeof(logBuffer));

      sprintf(logBuffer, "Std IUID from Worklist file=%s\nStd IUID from incoming DICOM file=%s", workliststdIUID, incomingFileStdIUID);

      OrthancPluginLogInfo(context, logBuffer);
      return true;
    }

    return false;
  }
  catch (...)
  {
    return false;
  }
 
}



OrthancPluginErrorCode OnStoredCallback(const OrthancPluginDicomInstance* instance,
                                        const char* instanceId)
{
  OrthancPluginContext* context = OrthancPlugins::GetGlobalContext();
  char buffer[1024];
  sprintf(buffer, "Just received a DICOM instance of size %d and ID %s from origin %d (AET %s)", 
          (int) OrthancPluginGetInstanceSize(context, instance), instanceId, 
          OrthancPluginGetInstanceOrigin(context, instance),
          OrthancPluginGetInstanceRemoteAet(context, instance));
  OrthancPluginLogInfo(context, buffer);


  char studyInstanceUIDStr[64] = {0}; // Max length of UID can be 64
  char buffer1[512] = {0};

  char* jsonIncomingDICOM;
  jsonIncomingDICOM = OrthancPluginGetInstanceSimplifiedJson(context, instance);

  // strcpy(studyInstanceUIDStr, "1.2.276.0.7230010.3.2.101");
  getDicomTag(jsonIncomingDICOM, "StudyInstanceUID", studyInstanceUIDStr, 64);  

  sprintf(buffer1, "\n******\n\n    StudyInstanceUID=%s\n\n*********\n", studyInstanceUIDStr);
  OrthancPluginLogInfo(context, buffer1);

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
            std::string result;
            bool ok = ReadAndVerifyWorklistFile(result, filePath, studyInstanceUIDStr);

            if(ok){

              memset(buffer1, 0, sizeof(buffer1));
              sprintf(buffer1, "\n******\n    Removing the worklist file %s\n*********\n", filePath);
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


    OrthancPlugins::LogWarning("WorklistFilePurger :  plugin is initializing");
    OrthancPluginSetDescription(c, "Delete worklist file after receiving an instance in Orthanc PACS");


    OrthancPlugins::OrthancConfiguration configuration;

    OrthancPlugins::OrthancConfiguration worklists;
    configuration.GetSection(worklists, "Worklists");

    bool enabled = worklists.GetBooleanValue("Enable", false);
    if (enabled)
    {
      if (worklists.LookupStringValue(folder_, "Database"))
      {
        OrthancPluginRegisterOnStoredInstanceCallback(OrthancPlugins::GetGlobalContext(), OnStoredCallback);
      }
      else
      {
        OrthancPlugins::LogError("WorklistFilePurger : The configuration option \"Worklists.Database\" must contain a path");
        return -1;
      }

    }
    else
    {
      OrthancPlugins::LogWarning("WorklistFilePurger : Worklist server is disabled by the configuration file");
    }

    return 0;
    
  }


  ORTHANC_PLUGINS_API void OrthancPluginFinalize()
  {
    OrthancPlugins::LogWarning("Worlist File Purger plugin is finalizing");
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetName()
  {
    return "worklist-files-purger";
  }


  ORTHANC_PLUGINS_API const char* OrthancPluginGetVersion()
  {
    return WORKLIST_FILE_PURGER_VERSION;
  }

  

}
