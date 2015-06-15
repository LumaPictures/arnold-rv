#include <ai.h>
#include <sstream>
#include <algorithm>
#include <gcore/gcore>
#include <gnet/gnet>

#if AI_VERSION_ARCH_NUM > 4 || (AI_VERSION_ARCH_NUM == 4 && AI_VERSION_MAJOR_NUM >= 1)
#   define EXTENDED_DRIVER_API
#endif

#if 0
#  define FORCE_VIEW_NAME
#endif

AI_DRIVER_NODE_EXPORT_METHODS(RVDriverMtd);

class Client
{
public:
   
   Client(const std::string extraArgs="")
      : mSocket(0), mConn(0), mUseOCIO(false)
      , mRVStarted(false), mRVStartedWithOCIO(false)
      , mExtraArgs(extraArgs)
   {
   }
   
   ~Client()
   {
      close();
      
      if (mSocket)
      {
         AiMsgDebug("[rvdriver] Destroy socket");
         delete mSocket;
      }
   }
   
   bool connect(std::string hostname, int port, bool runRV=true, bool silent=false, int maxRetry=10, unsigned long waitFor=1000)
   {
      try
      {
         gnet::Host host(hostname, port);
         
         if (mSocket)
         {
            if (!silent)
            {
               AiMsgDebug("[rvdriver] Socket already created");
            }
            
            if (mSocket->host().address() != host.address() || mSocket->host().port() != host.port())
            {
               if (!silent)
               {
                  AiMsgDebug("[rvdriver] Host differs, destroy socket");
               }
               close();
               delete mSocket;
               mSocket = 0;
            }
            else if (mConn)
            {
               if (mConn->isValid())
               {
                  if (!silent)
                  {
                     AiMsgDebug("[rvdriver] Connection already established");
                  }
                  return true;
               }
               else
               {
                  // keep socket but close connection
                  if (!silent)
                  {
                     AiMsgDebug("[rvdriver] Close invalid connection");
                  }
                  mSocket->closeConnection(mConn);
                  mConn = 0;
               }
            }
         }
         
         if (!mSocket)
         {
            if (!silent)
            {
               AiMsgDebug("[rvdriver] Create socket");
            }
            mSocket = new gnet::TCPSocket(gnet::Host(hostname, port));
         }
         
         if (!silent)
         {
            AiMsgDebug("[rvdriver] Connect to host");
         }
         mConn = mSocket->connect();
      }
      catch (std::exception &err)
      {
         // No risk having mConn != 0 here
         if (mSocket)
         {
            delete mSocket;
            mSocket = 0;
         }
         
         if (runRV)
         {
            gcore::String cmd = "rv -network -networkPort " + gcore::String(port);
            if (mUseOCIO)
            {
               cmd += " -flags ModeManagerPreload=ocio_source_setup";
            }
            if (mExtraArgs.length() > 0)
            {
               cmd += " ";
               cmd += mExtraArgs;
            }
            
            if (!silent)
            {
               AiMsgInfo("[rvdriver] Starting RV \"%s\"...", cmd.c_str());
            }

            gcore::Process p;

            p.keepAlive(true);
            p.showConsole(true);
            p.captureOut(false);
            p.captureErr(false);
            p.redirectIn(false);

            if (p.run(cmd) == gcore::INVALID_PID)
            {
               if (!silent)
               {
                  AiMsgWarning("[rvdriver] Error while connecting: %s", err.what());
               }
               return false;
            }
            
            int retry = 0;
            while (retry < maxRetry)
            {
               gcore::Thread::SleepCurrent(waitFor);
               if (!silent)
               {
                  AiMsgDebug("[rvdriver] Retry connecting... (%d/%d)", retry+1, maxRetry);
               }
               if (connect(hostname, port, false, true, 0, 0))
               {
                  break;
               }
               ++retry;
            }
            
            if (retry >= maxRetry)
            {
               if (!silent)
               {
                  AiMsgWarning("[rvdriver] Failed to connect to RV after %d attempt(s)", maxRetry);
                  return false;
               }
            }

            mRVStarted = true;
            mRVStartedWithOCIO = mUseOCIO;

            /* The following code was unreliable on windows (stdout not flushed from RV?)
               Driver was waiting undefinitely at "p.read(tmp)"
            
            p.showConsole(true);
            p.captureOut(true);
            p.captureErr(true, true);
            
            if (p.run(cmd) == gcore::INVALID_PID)
            {
               AiMsgWarning("[rvdriver] Error while connecting: %s", err.what());
               return false;
            }
            
            // Wait until we get the "listening on port message"
            gcore::String output;
            gcore::String tmp;
            while (p.read(tmp) != -1)
            {
               output += tmp;
               if (output.find("INFO: listening on port") != std::string::npos)
               {
                  break;
               }
            }
            
            return connect(hostname, port, false);
            */
         }
         else
         {
            if (!silent)
            {
               AiMsgWarning("[rvdriver] Error while connecting: %s", err.what());
            }
            return false;
         }
      }
      
      return true;
   }
   
   void write(const std::string& msg)
   {
      if (!isAlive())
      {
         return;
      }
      
      try
      {
         mConn->write(msg.c_str(), msg.length());
      }
      catch (std::exception &err)
      {
         AiMsgWarning("[rvdriver] Error while writing: %s", err.what());
         close();
      }
   }
   
   void writeBucket(const std::string &header, void *pixels, size_t size)
   {
      if (!isAlive())
      {
         return;
      }
      
      try
      {
         mConn->write(header.c_str(), header.length());
         mConn->write((const char*)pixels, size);
      }
      catch (std::exception &err)
      {
         AiMsgWarning("[rvdriver] Error while writing: %s", err.what());
         close();
      }
   }
   
   void writeMessage(const std::string& msg)
   {
      std::ostringstream oss;
      oss << "MESSAGE " << msg.size() << " " << msg;
      write(oss.str());
   }
   
   void readOnce(std::string &s)
   {
      if (isAlive())
      {
         try
         {
            mConn->reads(s);
         }
         catch (std::exception &e)
         {
            AiMsgWarning("[rvdriver] Read failed: %s", e.what());
            s = "";
         }
      }
      else
      {
         s = "";
      }
   }
   
   void read()
   {
      bool done = false;
      char *bytes = 0;
      size_t len = 0;
      
      try
      {
         while (isAlive() && !done)
         {
            mConn->read(bytes, len);
            
            done = (len == 0);
            
            if (bytes)
            {
               AiMsgDebug("[rvdriver] Received \"%s\"", bytes);

               // Note: beware of multithreading if we need to write to the socket
               if (len >= 8 && !strncmp(bytes, "GREETING", 8))
               {
               }
               else if (len >= 8 && !strncmp(bytes, "PING 1 p", 8))
               {
                  AiMsgDebug("[rvdriver] PONG");
                  mConn->write("PONG 1 p", 8);
               }
               else if (len >= 7 && !strncmp(bytes, "MESSAGE", 7))
               {
                  if (strstr(bytes, "DISCONNECT") != 0)
                  {
                     AiMsgDebug("[rvdriver] Remotely disconnected");
                     free(bytes);
                     break;
                  }
               }
               
               free(bytes);
               bytes = 0;
            }
         }
      }
      catch (gnet::Exception &e)
      {
         AiMsgDebug("[rvdriver] Read thread terminated: %s", e.what());
      }
   }
   
   void close()
   {
      if (mSocket && mConn)
      {
         AiMsgDebug("[rvdriver] Close connection");
         mSocket->closeConnection(mConn);
         // closeConnection may not delete mConn if it is not a connection to it
         mConn = 0;
      }
   }
   
   bool isAlive() const
   {
      return (mSocket && mConn && mConn->isValid());
   }
   
   void useOCIO(bool onoff)
   {
      mUseOCIO = onoff;
   }
   
   bool startedRV() const
   {
      return mRVStarted;
   }
   
   bool startedRVWithOCIO() const
   {
      return mRVStartedWithOCIO;
   }
   
private:
   
   gnet::TCPSocket *mSocket;
   gnet::TCPConnection *mConn;
   bool mUseOCIO;
   bool mRVStarted;
   bool mRVStartedWithOCIO;
   std::string mExtraArgs;
};

unsigned int ReadFromConnection(void *data)
{
   Client *client = (Client*) data;
   client->read();
   return 1;
}

namespace
{
    enum RVDriverParams
    {
       p_host = 0,
       p_port,
       p_extra_args,
       p_color_correction,
       p_gamma,
       p_lut,
       p_ocio_profile,
       p_media_name,
       p_add_timestamp
    };
}

struct ShaderData
{
   void* thread;
   Client* client;
   std::string* media_name;
   int frame;
};

enum ColorCorrection
{
   CC_None,
   CC_sRGB,
   CC_Rec709,
   CC_Gamma_2_2,
   CC_Gamma_2_4,
   CC_Custom_Gamma,
   CC_LUT,
   CC_OCIO
};

static const char* ColorCorrectionNames[] =
{
   "None",
   "sRGB",
   "Rec709",
   "Gamma 2.2",
   "Gamma 2.4",
   "Custom Gamma",
   "LUT",
   "OCIO",
   NULL
};

node_parameters
{
   AiParameterSTR("host", "localhost");
   AiParameterINT("port", 45124);
   AiParameterSTR("extra_args", "");
   AiParameterENUM("color_correction", 0, ColorCorrectionNames);
   AiParameterFLT("gamma", 0.0f);
   AiParameterSTR("lut", "");
   AiParameterSTR("ocio_profile", "");
   AiParameterSTR("media_name", "");
   AiParameterBOOL("add_timestamp", false);
   
   AiMetaDataSetBool(mds, "gamma", "maya.hide", true);
   //AiMetaDataSetBool(mds, "media_name", "maya.hide", true);
   //AiMetaDataSetBool(mds, "add_timestamp", "maya.hide", true);

   AiMetaDataSetBool(mds, NULL, "display_driver", true);
   AiMetaDataSetBool(mds, NULL, "single_layer_driver", false);
   
   // MtoA (maya) specific metadata 
   AiMetaDataSetStr(mds, NULL, "maya.name", "aiAOVDriver");
   AiMetaDataSetStr(mds, NULL, "maya.translator", "rv");
   AiMetaDataSetStr(mds, NULL, "maya.attr_prefix", "rv_");
}

node_initialize
{
   AiMsgDebug("[rvdriver] Driver initialize");

   ShaderData* data = (ShaderData*) AiMalloc(sizeof(ShaderData));
   data->thread = NULL;
   data->client = NULL;
   data->media_name = NULL;
   data->frame = 0;
   AiDriverInitialize(node, true, data);
}

node_update
{
}

driver_supports_pixel_type
{
   return true;
}

driver_extension
{
   return NULL;
}

driver_open
{
   AiMsgDebug("[rvdriver] Driver open");
   
   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   
   const char* host = AiNodeGetStr(node, "host");
   // TODO: allow port to be a search range of form "45124-45128" ?
   int port = AiNodeGetInt(node, "port");
   bool use_timestamp = AiNodeGetBool(node, "add_timestamp");
   
   // Read color correcition settings
   float gamma = 1.0f;
   std::string lut = "";
   std::string ocio = "";
   ColorCorrection cc = (ColorCorrection) AiNodeGetInt(node, "color_correction");
   
   if (cc == CC_Gamma_2_2)
   {
      gamma = 2.2f;
      AiMsgInfo("[rvdriver] Color Correction: Using gamma 2.2");
   }
   else if (cc == CC_Gamma_2_4)
   {
      gamma = 2.4f;
      AiMsgInfo("[rvdriver] Color Correction: Using gamma 2.4");
   }
   else if (cc == CC_sRGB)
   {
      AiMsgInfo("[rvdriver] Color Correction: Using sRGB");
   }
   else if (cc == CC_Rec709)
   {
      AiMsgInfo("[rvdriver] Color Correction: Using Rec709");
   }
   else if (cc == CC_Custom_Gamma)
   {
      gamma = AiNodeGetFlt(node, "gamma");
      if (gamma <= 0.0f)
      {
         AiMsgInfo("[rvdriver] Color Correction: \"gamma\" less than or equal to 0. Check ARNOLD_RV_DRIVER_GAMMA environment variable");
         if (gcore::Env::IsSet("ARNOLD_RV_DRIVER_GAMMA"))
         {
            if (sscanf(gcore::Env::Get("ARNOLD_RV_DRIVER_GAMMA").c_str(), "%f", &gamma) != 1)
            {
               AiMsgInfo("[rvdriver] Color Correction: Invalid value for ARNOLD_RV_DRIVER_GAMMA environment. Default \"gamma\" to 1");
               gamma = 1.0f;
            }
            else
            {
               AiMsgInfo("[rvdriver] Color Correction: Using custom gamma %f", gamma);
            }
         }
         else
         {
            AiMsgInfo("[rvdriver] Color Correction: ARNOLD_RV_DRIVER_GAMMA environment not set. Default \"gamma\" to 1");
            gamma = 1.0f;
         }
      }
      else
      {
         AiMsgInfo("[rvdriver] Color Correction: Using custom gamma %f", gamma);
      }
   }
   else if (cc == CC_LUT)
   {
      struct stat st;
      
      lut = AiNodeGetStr(node, "lut");
      if (lut.length() == 0 || stat(lut.c_str(), &st) != 0)
      {
         AiMsgInfo("[rvdriver] Color Correction: Invalid LUT file \"%s\". Check ARNOLD_RV_DRIVER_LUT environment variable", lut.c_str());
         if (gcore::Env::IsSet("ARNOLD_RV_DRIVER_LUT"))
         {
            lut = gcore::Env::Get("ARNOLD_RV_DRIVER_LUT");
            if (lut.length() == 0 || stat(lut.c_str(), &st) != 0)
            {
               AiMsgInfo("[rvdriver] Color Correction: Invalid LUT file \"%s\". No color correction", lut.c_str());
               lut = "";
            }
            else
            {
               AiMsgInfo("[rvdriver] Color Correction: Using LUT file \"%s\"", lut.c_str());
            }
         }
         else
         {
            AiMsgInfo("[rvdriver] Color Correction: ARNOLD_RV_DRIVER_LUT environment not set. No color correction");
            lut = "";
         }
      }
      else
      {
         AiMsgInfo("[rvdriver] Color Correction: Using LUT file \"%s\"", lut.c_str());
      }
   }
   else if (cc == CC_OCIO)
   {
      struct stat st;
      
      ocio = AiNodeGetStr(node, "ocio_profile");
      if (ocio.length() == 0 || stat(ocio.c_str(), &st) != 0)
      {
         AiMsgInfo("[rvdriver] Color Correction: Invalid OCIO profile \"%s\". Check OCIO environment variable", ocio.c_str());
         if (gcore::Env::IsSet("OCIO"))
         {
            ocio = gcore::Env::Get("OCIO");
            if (ocio.length() == 0 || stat(ocio.c_str(), &st) != 0)
            {
               AiMsgInfo("[rvdriver] Color Correction: Invalid OCIO profile \"%s\". No color correction", ocio.c_str());
               ocio = "";
            }
            else
            {
               AiMsgInfo("[rvdriver] Color Correction: Using OCIO profile \"%s\"", ocio.c_str());
            }
         }
         else
         {
            AiMsgInfo("[rvdriver] Color Correction: OCIO environment not set. No color correction");
            ocio = "";
         }
      }
      else
      {
         // Override env value
         gcore::Env::Set("OCIO", ocio, true);
         AiMsgInfo("[rvdriver] Color Correction: Using OCIO profile \"%s\"", ocio.c_str());
      }
   }
   
   // Create client if needed
   if (data->client == NULL)
   {
      data->client = new Client(AiNodeGetStr(node, "extra_args"));
   }
   else
   {
      return;
   }
   
   // In case RV has not yet running, this will ensure that OCIO mode is loaded when when start it
   data->client->useOCIO(ocio.length() > 0);
   
   if (!data->client->connect(host, port))
   {
      AiMsgWarning("[rvdriver] Could not connect to %s:%d", host, port);
      if (data->media_name != NULL)
      {
         delete data->media_name;
      }
      data->media_name = NULL;
      return;
   }
   
   if (data->media_name == NULL)
   {
      // always add time stamp?
      std::string mn = AiNodeGetStr(node, "media_name");
      if (mn.length() == 0)
      {
         mn = gcore::Date().format("%Y-%m-%d_%H:%M:%S");
      }
      else if (use_timestamp)
      {
         mn += "_" + gcore::Date().format("%Y-%m-%d_%H:%M:%S");
      }
      AiMsgDebug("[rvdriver] Media name: %s", mn.c_str());
      
      data->media_name = new std::string(mn);
   }
   
   std::ostringstream oss;
   
   // Send greeting to RV
   std::string greeting = "rv-shell-1 arnold";
   oss << "NEWGREETING " << greeting.size() << " " << greeting;
   data->client->write(oss.str());
   
   data->client->readOnce(greeting);
   AiMsgDebug("[rvdriver] %s", greeting.c_str());
   
   // Disable Ping/Pong control: No
   //data->client->write("PINGPONGCONTROL 0 ");
   
   //  The newImageSource() function needs to be called for RV create
   //  space for the image. After this the pixel blocks will refer to
   //  the media name. The media name identifies where the
   //  pixels will go. newImageSource() will return the name of the
   //  new source, but we'll only need that to setup the source
   //  pixels not when we send tiles.
   //
   //  Put together the newImageSource() and newImageSourcePixels()
   //  calls.  Make a Mu lexical block and call the two
   //  newImageSourcePixels() calls with the result of
   //  newImageSource(). So the final string will look like this:
   //
   //      {
   //          let s = newImageSource(...);
   //          newImageSourcePixels(s, ...);
   //          newImageSourcePixels(s, ...);
   //      }
   
   // Create the list of AOVs in mu syntax
   int pixel_type;
   const char* aov_name;
   std::string aov_names = "";
   std::string aov_cmds = "";
   unsigned int i = 0;
   
   while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, NULL))
   {
      oss.str("");     
#ifdef FORCE_VIEW_NAME
      oss << "    newImageSourcePixels(src, frame, \"" << aov_name << "\", \"master\");\n";
#else
      oss << "    newImageSourcePixels(src, frame, \"" << aov_name << "\", nil);\n";
#endif
      
      // Make RGBA the 'first' layer
      if (!strcmp(aov_name, "RGBA"))
      {
         if (i > 0)
         {
            aov_names = std::string("\"") + aov_name + "\"," + aov_names;
         }
         else
         {
            aov_names = std::string("\"") + aov_name + "\"";
         }
         aov_cmds = oss.str() + aov_cmds;
      }
      else
      {
         if (i > 0)
         {
            aov_names += ",";
         }
         aov_names += std::string("\"") + aov_name + "\"";
         aov_cmds += oss.str();
      }
      
      ++i;
   }
   
   // View color correction setup
   std::string viewSetup = "";
   
   if (ocio.length() > 0)
   {
      // This won't work if RV hasn't ocio_source_setup package already loaded...
      //viewSetup += "  activateMode(\"OCIO Source Setup\");\n";
      if (data->client->startedRV())
      {
         if (!data->client->startedRVWithOCIO())
         {
            AiMsgWarning("[rvdriver] RV hasn't been started with OCIO mode preloaded. Color correction setup will not have any effect.");
         }
      }
      else
      {
         AiMsgWarning("[rvdriver] RV may not have been started with OCIO mode preloaded or may be using a different OCIO profile. If so, color correction setup will not have any effect.");
      }
   }
   else
   {
      if (data->client->startedRV())
      {
         if (data->client->startedRVWithOCIO())
         {
            AiMsgWarning("[rvdriver] RV has been started with OCIO mode preloaded. Any other color correction scheme will not have any effect.");
         }
      }
      else
      {
         AiMsgWarning("[rvdriver] RV may have been started with OCIO mode preloaded. If so, any other color correction scheme will not have any effect.");
      }
      
      if (lut.length() > 0)
      {
         // replace \ by /
         size_t p0 = 0;
         size_t p1 = lut.find('\\', p0);
         while (p1 != std::string::npos)
         {
            lut[p1] = '/';
            p0 = p1 + 1;
            p1 = lut.find('\\', p0);
         }
         
         // This actually doesn't work well... avoid switching between OCIO and other Color Correction Schemes
         // => Standard color correction menu stay greyed out
         //viewSetup += "  deactivateMode(\"OCIO Source Setup\");\n";
         
         viewSetup += "  setFloatProperty(\"#RVDisplayColor.color.gamma\", float[]{1.0});\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.color.sRGB\", int[]{0});\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.color.Rec709\", int[]{0});\n";
         viewSetup += "  readLUT(\"" + lut + "\", \"#RVDisplayColor\");\n";
         viewSetup += "  updateLUT();\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.lut.active\", int[]{1});";
      }
      else if (gamma > 0.0f)
      {
         char numbuf[64];
         sprintf(numbuf, "%f", gamma);
         
         // This actually doesn't work well... avoid switching between OCIO and other Color Correction Schemes
         // => Standard color correction menu stay greyed out
         //viewSetup += "  deactivateMode(\"OCIO Source Setup\");\n";
         
         viewSetup += "  setFloatProperty(\"#RVDisplayColor.color.gamma\", float[]{";
         viewSetup += numbuf;
         viewSetup += "});\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.color.sRGB\", int[]{";
         viewSetup += (cc == CC_sRGB ? "1" : "0");
         viewSetup += "});\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.color.Rec709\", int[]{";
         viewSetup += (cc == CC_Rec709 ? "1" : "0");
         viewSetup += "});\n";
         viewSetup += "  setIntProperty(\"#RVDisplayColor.lut.active\", int[]{0});";
      }
   }
   
   // There is no need to set the data window for region renders, bc the tiles place
   // themselves appropriately within the image.
   oss.str("");
   oss << "RETURNEVENT remote-eval * ";
   oss << "{ string media = \"" << *(data->media_name) << "\";" << std::endl;
   oss << "  bool found = false;" << std::endl;
   oss << "  string src = \"\";" << std::endl;
   oss << "  int frame = 1;" << std::endl;
   oss << "  for_each (source; nodesOfType(\"RVImageSource\")) {" << std::endl;
   oss << "    if (getStringProperty(\"%s.media.name\" % source)[0] == media) {" << std::endl;
   oss << "      found = true;" << std::endl;
   oss << "      src = source;" << std::endl;
   oss << "      break;" << std::endl;
   oss << "    }" << std::endl;
   oss << "  }" << std::endl;
   oss << "  if (!found) {" << std::endl;
   oss << "    src = newImageSource(media, ";  // name
   oss << (display_window.maxx - display_window.minx + 1) << ", ";  // w
   oss << (display_window.maxy - display_window.miny + 1) << ", ";  // h
   oss << (display_window.maxx - display_window.minx + 1) << ", ";  // uncrop w
   oss << (display_window.maxy - display_window.miny + 1) << ", ";  // uncrop h
   oss << "0, 0, 1.0, 4, 32, true, 1, 1, 24.0, ";  // x offset, y offset, pixel aspect, channels, bit-depth, floatdata, start frame, end frame, frame rate
   oss << "string[] {" << aov_names << "}, ";  // layers
#ifdef FORCE_VIEW_NAME
   oss << "string[] {\"master\"});" << std::endl;  // views [Note: without a view name defined, RV 4 (< 4.0.10) will just crash]
#else
   oss << "nil);" << std::endl;  // views
#endif
   oss << aov_cmds << std::endl;  // source pixel commands
   oss << "  } else {" << std::endl;
   oss << "    frame = getIntProperty(\"%s.image.end\" % src)[0] + 1;" << std::endl;
   oss << "    setIntProperty(\"%s.image.end\" % src, int[]{frame});" << std::endl;
   oss << aov_cmds << std::endl;
   oss << "  }" << std::endl;
   if (viewSetup.length() > 0)
   {
      oss << viewSetup << std::endl;
   }
   oss << "  setViewNode(nodeGroup(src));" << std::endl;
   oss << "  setFrameEnd(frame);" << std::endl;
   oss << "  setOutPoint(frame);" << std::endl;
   oss << "  setFrame(frame);" << std::endl;
   oss << "  frame;" << std::endl;
   oss << "}" << std::endl;
   
   std::string cmd = oss.str();
   
   AiMsgDebug("[rvdriver] Create image sources");
#ifdef _DEBUG
   std::cout << cmd << std::endl;
#endif
   data->client->writeMessage(cmd);
   
   std::string ret;
   int msgsz = 0;
   
   data->client->readOnce(ret);
   if (sscanf(ret.c_str(), "MESSAGE %d RETURN %d", &msgsz, &(data->frame)) == 2)
   {
      AiMsgDebug("[rvdriver] RV frame = %d", data->frame); 
   }
   
   if (data->thread == 0)
   {
      AiMsgDebug("[rvdriver] Start socket reading thread");
      data->thread = AiThreadCreate(ReadFromConnection, (void*)data->client, AI_PRIORITY_NORMAL);
   }
}

#ifdef EXTENDED_DRIVER_API
driver_needs_bucket
{
   return true;
}
#endif

driver_prepare_bucket
{
   // We could send something to RV here to denote the tile we're about to render
}

#ifdef EXTENDED_DRIVER_API
driver_process_bucket
{
}
#endif

driver_write_bucket
{
   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   
   if (data->media_name == NULL)
   {
      return;
   }
   
   //  Create the interp string. For RV the PIXELTILE looks
   //  like a python function call with keyword args. There
   //  can be no spaces in the string. The following arg
   //  variables are available, if they are not specified
   //  they get the defaults:
   //
   //      event            string, default = "pixel-block"
   //      tag (name)       unique identifier for this image
   //      media (name)     string, default = ""
   //      layer (name)     string, default = ""
   //      view  (name)     string, default = ""
   //      w (width)        value > 0, no default (required)
   //      h (height)       value > 0, no default (required)
   //      x (x tile pos)   any int, default is 0
   //      y (y tile pos)   any int, default is 0
   //      f (frame)        any int, default is 1
   //
   std::ostringstream oss;
   
   int yres = AiNodeGetInt(AiUniverseGetOptions(), "yres");
   
   size_t tile_size = bucket_size_x * bucket_size_y * 4 * sizeof(float);
   
   oss << "PIXELTILE(media=" << *(data->media_name);
   
   std::string layercmd1 = oss.str();

   oss.str("");
#ifdef FORCE_VIEW_NAME
   oss << ",view=master,w=" << bucket_size_x << ",h=" << bucket_size_y;
#else
   oss << ",w=" << bucket_size_x << ",h=" << bucket_size_y;
#endif
   oss << ",x=" << bucket_xo << ",y=" << (yres - bucket_yo - bucket_size_y);  // flip bucket coordinates vertically

   std::string layercmd2 = oss.str();
   
   int pixel_type;
   const void* bucket_data;
   const char* aov_name;
   AtRGBA* pixels = (AtRGBA*) AiMalloc(tile_size);
   
   while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, &bucket_data))
   {
      switch(pixel_type)
      {
         case AI_TYPE_RGBA:
         {
            for (int j = 0; j < bucket_size_y; ++j)
            {
               for (int i = 0; i < bucket_size_x; ++i)
               {
                  unsigned int in_idx = j * bucket_size_x + i;
                  // Flip vertically
                  unsigned int out_idx = (bucket_size_y - j - 1) * bucket_size_x + i;
                  AtRGBA* dest = &pixels[out_idx];

                  AtRGBA src = ((AtRGBA*)bucket_data)[in_idx];
                  dest->r = src.r;
                  dest->g = src.g;
                  dest->b = src.b;
                  dest->a = src.a;
               }
            }
            break;
         }
         case AI_TYPE_RGB:
         case AI_TYPE_VECTOR:
         case AI_TYPE_POINT:
         {
            for (int j = 0; j < bucket_size_y; ++j)
            {
               for (int i = 0; i < bucket_size_x; ++i)
               {
                  unsigned int in_idx = j * bucket_size_x + i;
                  // Flip vertically
                  unsigned int out_idx = (bucket_size_y - j - 1) * bucket_size_x + i;
                  AtRGBA* dest = &pixels[out_idx];

                  AtRGB src = ((AtRGB*)bucket_data)[in_idx];
                  dest->r = src.r;
                  dest->g = src.g;
                  dest->b = src.b;
                  dest->a = 0.0f;
               }
            }
            break;
         }
         case AI_TYPE_FLOAT:
         {
            for (int j = 0; j < bucket_size_y; ++j)
            {
               for (int i = 0; i < bucket_size_x; ++i)
               {
                  unsigned int in_idx = j * bucket_size_x + i;
                  // Flip vertically
                  unsigned int out_idx = (bucket_size_y - j - 1) * bucket_size_x + i;
                  AtRGBA* dest = &pixels[out_idx];

                  float src = ((float*)bucket_data)[in_idx];
                  dest->r = src;
                  dest->g = src;
                  dest->b = src;
                  dest->a = 0.0f;
               }
            }
            break;
         }
      }
      
      oss.str("");
      oss << layercmd1 << ",layer=" << aov_name << layercmd2 << ",f=" << data->frame << ") " << tile_size << " ";
      
#ifdef _DEBUG
      std::cout << oss.str() << "<data>" << std::endl;
#endif
      
      data->client->writeBucket(oss.str(), (void*)pixels, tile_size);
   } 
   
   AiFree(pixels);
}

driver_close
{
   AiMsgDebug("[rvdriver] Driver close");
}

node_finish
{
   AiMsgDebug("[rvdriver] Driver finish");

   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   
   if (data->media_name != NULL)
   {
      delete data->media_name;
      
      if (data->client->isAlive())
      {
         AiMsgDebug("[rvdriver] Send DISCONNECT message");
         data->client->write("MESSAGE 10 DISCONNECT");
      }
      
      AiThreadWait(data->thread);
      AiThreadClose(data->thread);
      data->thread = 0;
   }
   
   delete data->client;
   
   AiFree(data);
   AiDriverDestroy(node);
}

node_loader
{
   if (i == 0)
   {
      node->methods = (AtNodeMethods*) RVDriverMtd;
      node->output_type = AI_TYPE_RGBA;
      node->name = "rvdriver";
      node->node_type = AI_NODE_DRIVER;
      sprintf(node->version, AI_VERSION);
      return true;
   }
   else
   {
      return false;
   }
}
