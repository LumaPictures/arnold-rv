#include <ai.h>
#include <deque>
#include <sstream>
#include <gcore/gcore>
#include <gnet/gnet>

AI_DRIVER_NODE_EXPORT_METHODS(RVDriverMtd);

class Message
{
public:
   enum
   {
      max_length = 512
   };
   
   Message(const void* data, size_t length)
      : mLength(length)
   {
      mData = reinterpret_cast<const char*>(data);
   }
   
   const char* data() const
   {
      return mData;
   }
   
   size_t length() const
   {
      return mLength;
   }
    
private:
   const char* mData;
   size_t mLength;
};

class Client
{
public:

   Client()
      : mSocket(0), mConn(0)
   {
   }

   ~Client()
   {
      close();

      if (mSocket)
      {
         AiMsgInfo("[rvdriver] Destroy socket");
         delete mSocket;
      }
   }

   void read()
   {
      if (mConn)
      {
         bool done = false;
         char *bytes = 0;
         size_t len = 0;

         try
         {
            while (!done)
            {
               mConn->read(bytes, len);

               done = (len == 0);

               if (bytes)
               {
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
         }
      }
   }
   
   bool connect(std::string hostname, int port)
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

         // adjust connection buffer size to twice the size of a tile pixel data
         size_t ts = AiNodeGetInt(AiUniverseGetOptions(), "bucket_size");
         mConn->setBufferSize(2 * (ts * ts * 4 * sizeof(float)));
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

         return false;
      }
      
      return true;
   }
   
   void write(const Message& msg)
   {
      if (!mConn || !mConn->isValid())
      {
         return;
      }
      
      try
      {
         mConn->write(msg.data(), msg.length());
      }
      catch (std::exception &err)
      {
         close();
         AiMsgError("[rvdriver] Error while writing: %s", err.what());
      }
   }
   
   void write(const std::string& msg)
   {
      if (!mConn || !mConn->isValid())
      {
         return;
      }

      try
      {
         mConn->write(msg.c_str(), msg.length());
      }
      catch (std::exception &err)
      {
         close();
         AiMsgError("[rvdriver] Error while writing: %s", err.what());
      }
   }
   
   void writeMessage(const std::string& msg)
   {
      std::ostringstream oss;
      oss << "MESSAGE " << msg.size() << " " << msg;
      write(oss.str());
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
   }

private:
   
   gnet::TCPSocket *mSocket;
   gnet::TCPConnection *mConn;
};

namespace
{
    enum RVDriverParams
    {
       p_host = 0,
       p_port,
       p_gamma,
       p_imagename
    };
}

struct ShaderData
{
   void* thread;
   Client* client;
   std::string* media_name;
   int nchannels;
   
   ShaderData()
      : thread(NULL)
      , client(NULL)
      , media_name(NULL)
      , nchannels(-1)
   {
   }
};

node_parameters
{
   AiParameterSTR("host", "localhost");
   AiParameterINT("port", 45124);
   AiParameterFLT("gamma", 1.0f);
   AiParameterSTR("imagename", "");
   
   AiMetaDataSetBool(mds, "imagename", "maya.hide", true);
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

unsigned int ReadFromConnection(void *data)
{
   Client *client = (Client*) data;
   client->read();
   return 1;
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

   if (data->client == NULL)
   {
      data->client = new Client();
   }
   else
   {
      return;
   }

   if (!data->client->connect(host, port))
   {
      AiMsgWarning("[rvdriver] Could not connect to %s:%d", host, port);
      data->media_name = NULL;
      return;
   }

   if (data->media_name == NULL)
   {
      // always add time stamp?
      std::string mn = AiNodeGetStr(node, "imagename");
      mn += "_" + gcore::Date().format("%y%m%d-%H%M%S");
      AiMsgInfo("[rvdriver] Media name: %s", mn.c_str());

      data->media_name = new std::string(mn);
      data->thread = AiThreadCreate(ReadFromConnection, (void*)data->client, AI_PRIORITY_NORMAL);
   }

   std::ostringstream oss;

   // Send greeting to RV
   std::string greeting = "rv-shell-1 arnold";
   oss << "NEWGREETING " << greeting.size() << " " << greeting;
   data->client->write(oss.str());

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
      oss << "newImageSourcePixels(s, 1, \"" << aov_name << "\", nil);\n";
      aov_cmds += oss.str();
      i++;
   }
   aov_names += "}";

   // For now we always convert to RGBA, because RV does not allow layers with differing types/channels
   data->nchannels = 4;

   // There is no need to set the data window for region renders, bc the tiles place
   // themselves appropriately within the image.
   oss.str("");
   oss << "EVENT remote-eval * ";
   oss << "{ string media = \"" << *(data->media_name) << "\"; bool found = false;" << std::endl;
   oss << "  for_each (source; nodesOfType(\"RVImageSource\")) {" << std::endl;
   oss << "    if (getStringProperty(\"%s.media.name\" % source)[0] == media) {" << std::endl;
   oss << "      found = true;" << std::endl;
   oss << "      break;" << std::endl;
   oss << "    }" << std::endl;
   oss << "  }" << std::endl;
   oss << "  if (!found) {" << std::endl;
   oss << "    let s = newImageSource(media, ";  // name
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
   oss << "    setViewNode(nodeGroup(s));" << std::endl;  // make new source the currently viewed node
   oss << "  }" << std::endl;
   oss << "}" << std::endl;

   std::string cmd = oss.str();

   AiMsgInfo("[rvdriver] Create image sources");
   data->client->writeMessage(cmd);
}

driver_prepare_bucket
{
   // We could send something to RV here to denote the tile we're about to render
}

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
   std::string layercmd2 = ",f=1) ";
   std::string layercmd3 = " ";

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
      oss << layercmd1 << aov_name << layercmd2 << tile_size << layercmd3;

      data->client->write(oss.str());
      data->client->write(Message(pixels, tile_size));
      
      delete pixels;
   }   
}

driver_close
{
   AiMsgInfo("[rvdriver] Driver close");

   ShaderData *data = (ShaderData*) AiDriverGetLocalData(node);
   if (data->media_name == NULL)
   {
      return;
   }
}

node_finish
{
   AiMsgInfo("[rvdriver] Driver finish");
   
   ShaderData *data = (ShaderData*)AiDriverGetLocalData(node);
   
   if (data->media_name != NULL)
   {
      delete data->media_name;
      
      AiMsgInfo("[rvdriver] Send DISCONNECT message");
      data->client->write("MESSAGE 10 DISCONNECT");
      
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
      node->name = "driver_rv";
      node->node_type = AI_NODE_DRIVER;
      sprintf(node->version, AI_VERSION);
      return true;
   }
   else
   {
      return false;
   }
}