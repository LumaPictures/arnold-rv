#include <ai.h>
#include <sstream>
#include <algorithm>
#include <gcore/gcore>
#include <gnet/gnet>

#if AI_VERSION_ARCH_NUM > 4 || (AI_VERSION_ARCH_NUM == 4 && AI_VERSION_MAJOR_NUM >= 1)
#   define EXTENDED_DRIVER_API
#endif

AI_DRIVER_NODE_EXPORT_METHODS(RVDriverMtd);

class Message
{
public:
   
   typedef void (*FreeFunc)(void*);
   
   Message()
      : mData(0), mLength(0), mFree(0)
   {
   }
   
   Message(void* data, size_t length, FreeFunc freeData=0)
      : mData(data), mLength(length), mFree(freeData)
   {
   }
   
   Message(const Message &rhs)
      : mData(rhs.mData), mLength(rhs.mLength), mFree(rhs.mFree)
   {
      rhs.mFree = 0;
   }
   
   ~Message()
   {
      if (mData && mFree)
      {
         mFree(mData);
      }
   }
   
   Message& operator=(const Message &rhs)
   {
      if (this != &rhs)
      {
         mData = rhs.mData;
         mLength = rhs.mLength;
         mFree = rhs.mFree;
         rhs.mFree = 0;
      }
      return *this;
   }
   
   const char* bytes() const
   {
      return reinterpret_cast<const char*>(mData);
   }
   
   size_t length() const
   {
      return mLength;
   }
   
private:
   
   void* mData;
   size_t mLength;
   mutable FreeFunc mFree;
};

class QueueItem
{
public:
   QueueItem()
   {
   }
   
   QueueItem(const std::string &s)
      : mStr(s)
   {
   }
   
   QueueItem(const Message &m)
      : mMsg(m)
   {
   }
   
   QueueItem(const QueueItem &rhs)
      : mStr(rhs.mStr), mMsg(rhs.mMsg)
   {
   }
   
   ~QueueItem()
   {
   }
   
   QueueItem& operator=(const QueueItem &rhs)
   {
      if (this != &rhs)
      {
         mStr = rhs.mStr;
         mMsg = rhs.mMsg;
      }
      return *this;
   }
   
   void writeTo(gnet::TCPConnection *conn)
   {
      if (conn && conn->isValid())
      {
         if (mStr.length() > 0)
         {
            //AiMsgInfo("[rvdriver] Writing string (%lu) to socket", mStr.length());
            conn->write(mStr.c_str(), mStr.length());
         }
         else if (mMsg.length() > 0)
         {
            //AiMsgInfo("[rvdriver] Writing Message (%lu) to socket", mMsg.length());
            conn->write(mMsg.bytes(), mMsg.length());
         }
      }
   }

private:
   
   std::string mStr;
   Message mMsg;
};

typedef std::deque<QueueItem> Queue;

class Client
{
public:
   
   static unsigned int WriteThread(void *data)
   {
      Client *client = (Client*) data;
      client->write();
      return 1;
   }
   
public:
   
   Client(bool serializeWrites=false)
      : mSocket(0), mConn(0), mSerialize(serializeWrites), mWriteThread(0)
   {
      if (mSerialize)
      {
         AiMsgInfo("[rvdriver] Create serialization mutex");
         AiCritSecInitRecursive(&mMutex);
      }
   }
   
   ~Client()
   {
      close();
      
      if (mSocket)
      {
         AiMsgInfo("[rvdriver] Destroy socket");
         delete mSocket;
      }
      
      if (mSerialize)
      {
         AiMsgInfo("[rvdriver] Destroy serialization mutex");
         AiCritSecClose(&mMutex);
         mQueue.clear();
      }
   }
   
   bool connect(std::string hostname, int port, bool runRV=true)
   {
      try
      {
         gnet::Host host(hostname, port);
         
         if (mSocket)
         {
            AiMsgInfo("[rvdriver] Socket already created");
            if (mSocket->host().address() != host.address() || mSocket->host().port() != host.port())
            {
               AiMsgInfo("[rvdriver] Host differs, destroy socket");
               close();
               delete mSocket;
               mSocket = 0;
            }
            else if (mConn)
            {
               if (mConn->isValid())
               {
                  AiMsgInfo("[rvdriver] Connection already established");
                  return true;
               }
               else
               {
                  // keep socket but close connection
                  AiMsgInfo("[rvdriver] Close invalid connection");
                  mSocket->closeConnection(mConn);
                  mConn = 0;
               }
            }
         }
         
         if (!mSocket)
         {
            AiMsgInfo("[rvdriver] Create socket");
            mSocket = new gnet::TCPSocket(gnet::Host(hostname, port));
         }
         
         AiMsgInfo("[rvdriver] Connect to host");
         mConn = mSocket->connect();

         // start reading thread
         if (mSerialize)
         {
            AiMsgInfo("[rvdriver] Starting socket writing thread");
            mWriteThread = AiThreadCreate(WriteThread, (void*)this, AI_PRIORITY_NORMAL);
         }
      }
      catch (std::exception &err)
      {
         AiMsgWarning("[rvdriver] Error while connecting: %s", err.what());
         
         // No risk having mConn != 0 here
         
         if (mSocket)
         {
            delete mSocket;
            mSocket = 0;
         }

         if (runRV)
         {
            AiMsgInfo("[rvdriver] Trying to launch RV...");
            
            gcore::String cmd = "rv -network -networkPort " + gcore::String(port);
            AiMsgInfo("[rvdriver]   %s", cmd.c_str());

            gcore::Process p;
            
            p.keepAlive(true);
            p.showConsole(false);
            p.captureOut(true);
            p.captureErr(true, true);

            if (p.run(cmd) == gcore::INVALID_PID)
            {
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
         }
         else
         {
            return false;
         }
      }
      
      return true;
   }
   
   void write(Message &msg)
   {
      if (!isAlive())
      {
         return;
      }
      
      if (mSerialize)
      {
         AiCritSecEnter(&mMutex);
         QueueItem item(msg);
         mQueue.push_back(item);
         AiCritSecLeave(&mMutex);
         return;
      }
      
      try
      {
         mConn->write(msg.bytes(), msg.length());
      }
      catch (std::exception &err)
      {
         AiMsgWarning("[rvdriver] Error while writing: %s", err.what());
         close();
      }
   }
   
   void write(const std::string& msg)
   {
      if (!isAlive())
      {
         return;
      }
      
      if (mSerialize)
      {
         AiCritSecEnter(&mMutex);
         QueueItem item(msg);
         mQueue.push_back(item);
         AiCritSecLeave(&mMutex);
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
   
   void writeMessage(const std::string& msg)
   {
      std::ostringstream oss;
      oss << "MESSAGE " << msg.size() << " " << msg;
      write(oss.str());
   }
   
   void write()
   {
      while (isAlive())
      {
         AiCritSecEnter(&mMutex);
         while (mQueue.size() > 0)
         {
            try
            {
               mQueue.front().writeTo(mConn);
            }
            catch (std::exception &e)
            {
               AiMsgWarning("[rvdriver] Write failed: %s", e.what());
            }
            mQueue.pop_front();
         }
         AiCritSecLeave(&mMutex);
         
         // sleep a second
         gcore::Thread::SleepCurrent(1000);
      }
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
               AiMsgInfo("[rvdriver] Received \"%s\"", bytes);

               // Note: beware of multithreading if we need to write to the socket
               if (len >= 8 && !strncmp(bytes, "GREETING", 8))
               {
               }
               else if (len >= 8 && !strncmp(bytes, "PING 1 p", 8))
               {
                  //mConn->write("PONG 1 p");
               }
               else if (len >= 7 && !strncmp(bytes, "MESSAGE", 7))
               {
               }
               
               free(bytes);
               bytes = 0;
            }
         }
      }
      catch (gnet::Exception &e)
      {
         AiMsgInfo("[rvdriver] Read thread terminated: %s", e.what());
      }
   }
   
   void close()
   {
      if (mSocket && mConn)
      {
         AiMsgInfo("[rvdriver] Close connection");
         mSocket->closeConnection(mConn);
         // closeConnection may not delete mConn if it is not a connection to it
         mConn = 0;
      }

      if (mSerialize && mWriteThread)
      {
         AiMsgInfo("[rvdriver] Waiting socket writing thread to complete");
         AiThreadWait(mWriteThread);
         AiThreadClose(mWriteThread);
         mWriteThread = 0;
      }
   }

   bool isAlive() const
   {
      return (mSocket && mConn && mConn->isValid());
   }
   
private:
   
   gnet::TCPSocket *mSocket;
   gnet::TCPConnection *mConn;
   bool mSerialize;
   Queue mQueue;
   AtCritSec mMutex;
   void *mWriteThread;
};

unsigned int ReadFromConnection(void *data)
{
   Client *client = (Client*) data;
   client->read();
   return 1;
}

void FreeTile(void *data)
{
   AtRGBA *pixels = (AtRGBA*) data;
   delete[] pixels;
}

namespace
{
    enum RVDriverParams
    {
       p_host = 0,
       p_port,
       p_gamma,
       p_lut,
       p_media_name,
       p_add_timestamp,
       p_serialize_io
    };
}

struct ShaderData
{
   void* thread;
   Client* client;
   std::string* media_name;
   int nchannels;
   int frame;
   
   ShaderData()
      : thread(NULL)
      , client(NULL)
      , media_name(NULL)
      , nchannels(-1)
      , frame(1)
   {
   }
};

node_parameters
{
   AiParameterSTR("host", "localhost");
   AiParameterINT("port", 45124);
   AiParameterFLT("gamma", 1.0f);
   AiParameterSTR("lut", "");
   AiParameterSTR("media_name", "");
   AiParameterBOOL("add_timestamp", false);
   AiParameterBOOL("serialize_io", false);
   
   //AiMetaDataSetBool(mds, "media_name", "maya.hide", true);
   //AiMetaDataSetBool(mds, "add_timestamp", "maya.hide", true);
   //AiMetaDataSetBool(mds, "serialize_io", "maya.hide", true);
   AiMetaDataSetStr(mds, NULL, "maya.translator", "rv");
   AiMetaDataSetStr(mds, NULL, "maya.attr_prefix", "rv_");
   AiMetaDataSetBool(mds, NULL, "display_driver", true);
}

node_initialize
{
   AiMsgInfo("[rvdriver] Driver initialize");

   ShaderData* data = (ShaderData*) AiMalloc(sizeof(ShaderData));
   data->thread = NULL;
   data->client = NULL;
   data->media_name = NULL;
   data->nchannels = -1;
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
   AiMsgInfo("[rvdriver] Driver open");
   
   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   
   const char* host = AiNodeGetStr(node, "host");
   // TODO: allow port to be a search range of form "45124-45128" ?
   int port = AiNodeGetInt(node, "port");
   bool use_timestamp = AiNodeGetBool(node, "add_timestamp");
   
   if (data->client == NULL)
   {
      data->client = new Client(AiNodeGetBool(node, "serialize_io"));
   }
   else
   {
      return;
   }
   
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
      AiMsgInfo("[rvdriver] Media name: %s", mn.c_str());

      data->media_name = new std::string(mn);
   }

   std::ostringstream oss;

   // Send greeting to RV
   std::string greeting = "rv-shell-1 arnold";
   oss << "NEWGREETING " << greeting.size() << " " << greeting;
   data->client->write(oss.str());

   data->client->readOnce(greeting);
   AiMsgInfo("[rvdriver] %s", greeting.c_str());

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
   std::string aov_names = "string[] {";
   std::string aov_cmds = "";
   data->nchannels = 0;
   unsigned int i = 0;

   while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, NULL))
   {
      if (i > 0)
      {
         aov_names += ",";
      }

      aov_names += std::string("\"") + aov_name + "\"";
      
      oss.str("");
      oss << "    newImageSourcePixels(src, frame, \"" << aov_name << "\", nil);\n";
      aov_cmds += oss.str();

      i++;
   }
   aov_names += "}";

   // For now we always convert to RGBA, because RV does not allow layers with differing types/channels
   data->nchannels = 4;

   // Activate lut or gamma profile
   float gamma = AiNodeGetFlt(node, "gamma");
   std::string lut = AiNodeGetStr(node, "lut");
   if (lut.length() == 0 && gcore::Env::IsSet("ARNOLD_RV_DRIVER_LUT"))
   {
      lut = gcore::Env::Get("ARNOLD_RV_DRIVER_LUT");
   }

   struct stat st;
   std::string viewSetup = "";

   if (lut.length() > 0 && stat(lut.c_str(), &st) == 0)
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
      viewSetup  = "  setFloatProperty(\"#RVDisplayColor.color.gamma\", float[]{1.0});\n";
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

      viewSetup  = "  setFloatProperty(\"#RVDisplayColor.color.gamma\", float[]{";
      viewSetup += numbuf;
      viewSetup += "});\n";
      viewSetup += "  setIntProperty(\"#RVDisplayColor.color.sRGB\", int[]{0});\n";
      viewSetup += "  setIntProperty(\"#RVDisplayColor.color.Rec709\", int[]{0});\n";
      viewSetup += "  setIntProperty(\"#RVDisplayColor.lut.active\", int[]{0});";
   }

   // There is no need to set the data window for region renders, bc the tiles place
   // themselves appropriately within the image.
   oss.str("");
   //oss << "EVENT remote-eval * ";
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
   oss << "0, 0, 1.0, ";  // x offset, y offset, pixel aspect
   oss << data->nchannels << ", ";  // channels
   oss << "32, false, 1, 1, 24.0, ";  // bit-depth, nofloat, start frame, end frame, frame rate
   oss << aov_names << ", ";  // layers
   oss << "nil);" << std::endl;  // views
   oss << "    " << aov_cmds << std::endl;  // source pixel commands
   oss << "  } else {" << std::endl;
   oss << "    frame = getIntProperty(\"%s.image.end\" % src)[0] + 1;" << std::endl;
   oss << "    setIntProperty(\"%s.image.end\" % src, int[]{frame});" << std::endl;
   oss << "    " << aov_cmds << std::endl;
   oss << "  }" << std::endl;
   if (viewSetup.length() > 0)
   {
      oss << viewSetup << std::endl;
   }
   oss << "  setViewNode(nodeGroup(src));" << std::endl; // make source the currently viewed node
   oss << "  setFrameEnd(frame);" << std::endl;
   oss << "  setOutPoint(frame);" << std::endl;
   oss << "  setFrame(frame);" << std::endl;
   oss << "  frame;" << std::endl;
   oss << "}" << std::endl;

   std::string cmd = oss.str();

   AiMsgInfo("[rvdriver] Create image sources");
   data->client->writeMessage(cmd);

   std::string ret;
   int msgsz = 0;

   data->client->readOnce(ret);
   if (sscanf(ret.c_str(), "MESSAGE %d RETURN %d", &msgsz, &(data->frame)) == 2)
   {
      AiMsgInfo("[rvdriver] RV frame = %d", data->frame); 
   }

   if (data->thread == 0)
   {
      AiMsgInfo("[rvdriver] Start socket reading thread");
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
   
   size_t tile_size = bucket_size_x * bucket_size_y * data->nchannels * sizeof(float);
   
   oss << "PIXELTILE(media=" << *(data->media_name) << ",w=" << bucket_size_x << ",h=" << bucket_size_y;
   oss << ",x=" << bucket_xo << ",y=" << (yres - bucket_yo - bucket_size_y);  // flip bucket coordinates vertically
   oss << ",layer=";

   std::string layercmd1 = oss.str();
   
   int pixel_type;
   const void* bucket_data;
   const char* aov_name;
   
   while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, &bucket_data))
   {
      AtRGBA* pixels = new AtRGBA[bucket_size_x * bucket_size_y];
      
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
      oss << layercmd1 << aov_name << ",f=" << data->frame << ") " << tile_size << " ";
      
      Message msg(pixels, tile_size, FreeTile);
      
      data->client->write(oss.str());
      data->client->write(msg);
   }   
}

driver_close
{
   AiMsgInfo("[rvdriver] Driver close");
}

node_finish
{
   AiMsgInfo("[rvdriver] Driver finish");
   
   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   
   if (data->media_name != NULL)
   {
      delete data->media_name;
      
      if (data->client->isAlive())
      {
         AiMsgInfo("[rvdriver] Send DISCONNECT message");
         data->client->write("MESSAGE 10 DISCONNECT");
      }
      
      AiThreadWait(data->thread);
      AiThreadClose(data->thread);
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