#include <ai.h>
#include <ai_critsec.h>
#include <ai_drivers.h>
#include <ai_filters.h>
#include <ai_msg.h>
#include <ai_render.h>
#include <ai_universe.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <stdio.h>
#include <iostream>
#include <deque>


// Support driver API change in Arnold 4.1
#if AI_VERSION_ARCH_NUM > 4 || (AI_VERSION_ARCH_NUM == 4 && AI_VERSION_MAJOR_NUM >= 1)
    #define ARNOLD_DRIVER_API_4_1
#endif


using namespace std;
using boost::asio::ip::tcp;


typedef boost::shared_ptr<std::string> string_ptr;

AI_DRIVER_NODE_EXPORT_METHODS(RVDriverMtd);


class Message
{
public:
    enum { max_length = 512 };

    Message(const void* data, size_t length)
        : length_(length)
    {
        data_ = reinterpret_cast<const char*>(data);
    }

    const char* data() const
    {
        return data_;
    }

    size_t length() const
    {
        return length_;
    }

private:
    const char* data_;
    size_t length_;
};

typedef std::deque<Message> message_queue;

class Client
{
public:
    Client(boost::asio::io_service& io_service)
        : io_service_(io_service),
          socket_(io_service)
    {}

    bool connect(std::string host, int port)
    {
        try {
            tcp::resolver resolver(io_service_);
            tcp::resolver::query query(host, boost::lexical_cast<std::string>(port).c_str());
            tcp::resolver::iterator iterator = resolver.resolve (query);
            boost::system::error_code err = boost::asio::error::host_not_found;
            tcp::resolver::iterator end;
            while (err && iterator != end) {
                socket_.close();
                AiMsgInfo("connecting");
                socket_.connect(*iterator++, err);
            }

            if (err) {
                AiMsgError("Host not found");
                return false;
            }
        } catch (boost::system::system_error &err) {
            AiMsgError("Error while connecting: %s", err.what());
            return false;
        }
        return true;
    }

    void write(const Message& msg)
    {
        try {
            boost::asio::write(socket_,
                boost::asio::buffer(msg.data(), msg.length()));
        } catch (boost::system::system_error &err) {
            do_close();
            AiMsgError("Error while writing: %s", err.what());
        }
    }

    void write(const std::string& msg)
    {
        try {
            boost::asio::write(socket_,
                boost::asio::buffer(msg, msg.length()));
        } catch (boost::system::system_error &err) {
            do_close();
            AiMsgError("Error while writing: %s", err.what());
        }
    }

    void write_message(const std::string& msg)
    {
        boost::format messageFormat = boost::format("MESSAGE %1% %2%") % msg.size() % msg;
        write(messageFormat.str());
    }

    void close()
    {
        io_service_.post(boost::bind(&Client::do_close, this));
    }

private:

    void handle_connect(const boost::system::error_code& error)
    {
        if (!error)
        {
            cout << "connected" << endl;
            AiMsgInfo("connected");
        }
    }

    void do_close()
    {
        socket_.close();
    }

private:
    boost::asio::io_service& io_service_;
    tcp::socket socket_;
    message_queue write_msgs_;
};


void formatDateTime(
    const std::string& format,
    const boost::posix_time::ptime& date_time,
    std::string& result)
  {
    boost::posix_time::time_facet * facet =
      new boost::posix_time::time_facet(format.c_str());
    std::ostringstream stream;
    stream.imbue(std::locale(stream.getloc(), facet));
    stream << date_time;
    result = stream.str();
  }


namespace
{

    enum RVDriverParams
    {
       p_host,
       p_port,
       p_gamma
    };
}

struct ShaderData
{
   void* thread;
   Client* client;
   boost::asio::io_service* io_service;
   boost::asio::io_service::work* work;
   std::string* media_name;
   int nchannels;
   ShaderData() : thread(NULL),
                  client(NULL),
                  io_service(NULL),
                  work(NULL),
                  media_name(NULL),
                  nchannels(-1)
   {cout << "ShaderData()" << endl;};
};


node_parameters
{
   AiParameterSTR("host", "127.0.0.1");
   AiParameterINT("port", 45124);
   AiParameterSTR("filename", "");
   AiMetaDataSetBool(mds, "filename", "maya.hide", true);

   AiMetaDataSetStr(mds, NULL, "maya.translator", "rv");
   AiMetaDataSetStr(mds, NULL, "maya.attr_prefix", "");
   AiMetaDataSetBool(mds, NULL, "display_driver", true);
//   AiMetaDataSetBool(mds, NULL, "maya.hide", true);
}

node_initialize
{
   cout << "driver INIT" << endl;
   ShaderData* data = (ShaderData*)AiMalloc(sizeof(ShaderData));
   data->thread = NULL;
   data->client = NULL;
   data->io_service = NULL;
   data->work = NULL;
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

#ifdef ARNOLD_DRIVER_API_4_1
driver_needs_bucket
{
   return true;
}
#endif

driver_extension
{
   return NULL;
}

unsigned int io_service_run(void * data)
{
   boost::asio::io_service* io_service = (boost::asio::io_service*)data;
   cout << "running ioservice" << endl;
   io_service->run();
   return 1;
}

driver_open
{
   cout << "driver open" << endl;
   ShaderData *data = (ShaderData*)AiDriverGetLocalData(node);

   const char* host = AiNodeGetStr(node, "host");
   // TODO: allow port to be a search range of form "45124-45128" ?
   int  port = AiNodeGetInt(node, "port");

   if (data->client == NULL)
   {
      data->io_service = new boost::asio::io_service;
      data->work = new boost::asio::io_service::work(*data->io_service);
      data->client = new Client(*data->io_service);
   }
   else
   {
      // assume we're still connected
      return;
   }
   cout << "connect" << endl;

   if (!data->client->connect(host, port))
   {
      data->media_name = NULL;
      data->thread = NULL;
      cout << "return" << endl;
      return;
   }

   // Generate a unique media name. Cannot contain spaces. Everything after the last slash is
   // stripped from the name displayed in RV, so we put the timestamp in front followed by a slash
   // so it won't clutter the catalog.

   if (data->media_name == NULL)
   {
      data->media_name = new std::string(AiNodeGetStr(node, "filename"));

      AiMsgInfo( "spawning thread");
      //data->thread = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));
      data->thread = AiThreadCreate(io_service_run, (void*)data->io_service, AI_PRIORITY_NORMAL);
   }

   cout << "greet" << endl;
   std::string greeting = "rv-shell-1 arnold";
   boost::format greetingFormat = boost::format("NEWGREETING %1% %2%") % greeting.size() % greeting;
   data->client->write(greetingFormat.str());

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
   // create the list of AOVs in mu syntax
   int         pixel_type;
   const char* aov_name;
   std::string aov_names = "string[] {";
   std::string aov_cmds = "";
   data->nchannels = 0;
   unsigned int i = 0;
   while (AiOutputIteratorGetNext(iterator, &aov_name, &pixel_type, NULL))
   {
      if (i > 0)
         aov_names += ",";
      aov_names += std::string("\"") + aov_name + "\"";
      aov_cmds += (boost::format("newImageSourcePixels( s, 1, \"%1%\", nil);\n") % aov_name).str();
      i++;
   }
   aov_names +=  "}";

   data->nchannels = 4; // for now we always convert to RGBA, because RV does not allow layers with differing types/channels


   boost::format newImageSource("EVENT remote-eval * "
         "{ string media = \"%1%\"; bool found = false;\n"
         "for_each (source; nodesOfType(\"RVImageSource\")) {\n"
         "  if (getStringProperty(\"%%s.media.name\" %% source)[0] == media) {\n"
         "    found = true;\n"
         "    break;\n}}\n"
         "if (!found) {\n"
         "  let s = newImageSource( media, %2%, %3%, "     // name, w, h
         "%4%, %5%, 0, 0, "                                // uncrop w, h, x-off, y-off,
         "1.0, %6%, 32, false, "                           // pixel aspect, channels, bit-depth, nofloat
         "1, 1, 24.0, "                                    // fs, fe, fps
         "%7%, "                                           // layers
         "nil"                                             // views
         ");\n %8%\n"                                      // source pixel commands
         "setViewNode(nodeGroup(s));"                      // make the new source the currently viewed node
         "}}; ");
         //"print(\"%%s\\n\" %% getStringProperty(\"%%s.media.name\" %% s)); print(\"%%s\\n\" %% getStringProperty(\"%%s.media.location\" %% s)); }; ");

   // there is no need to set the data window for region renders, bc the tiles place
   // themselves appropriately within the image.
   newImageSource % *data->media_name;
   newImageSource % (display_window.maxx - display_window.minx +1);
   newImageSource % (display_window.maxy - display_window.miny +1);
   newImageSource % (display_window.maxx - display_window.minx +1);
   newImageSource % (display_window.maxy - display_window.miny +1);
   newImageSource % data->nchannels;
   newImageSource % aov_names;
   newImageSource % aov_cmds;
   boost::format messageFormat = boost::format("MESSAGE %1% %2%") % newImageSource.size() % newImageSource;
   cout << messageFormat << endl;
   data->client->write(messageFormat.str());

   cout << "data window x " << data_window.minx << ", " << data_window.maxx << endl;
   cout << "data window y " << data_window.miny << ", " << data_window.maxy << endl;
}

driver_prepare_bucket
{
   // we could send something to RV here to denote the tile we're about to render
}

#ifdef ARNOLD_DRIVER_API_4_1
driver_process_bucket
{
}
#endif

driver_write_bucket
{
//   AiMsgDebug("[rvdriver] write bucket (%d, %d)", bucket_xo, bucket_yo);
   ShaderData *data = (ShaderData*)AiDriverGetLocalData(node);

   // we failed to connect in driver_open
   if (data->media_name == NULL)
      return;

   //
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
   int yres = AiNodeGetInt(AiUniverseGetOptions(), "yres");

//   cout << "res            " << yres << endl;
//   cout << "bucket origin  " << bucket_xo << ", " << bucket_yo << endl;
//   cout << "flipped origin " << bucket_xo << ", " << (yres - bucket_yo- bucket_size_y) << endl;

   // create the static portion of the message
   size_t tile_size = bucket_size_x * bucket_size_y * data->nchannels * sizeof(float);
   boost::format tileFormat = boost::format( "PIXELTILE("
                                 "media=%1%,w=%2%,h=%3%,x=%4%,y=%5%,layer=%6%,f=1) %7% ");
   tileFormat % *data->media_name;
   tileFormat % bucket_size_x;
   tileFormat % bucket_size_y;
   tileFormat % bucket_xo;
   tileFormat % (yres - bucket_yo- bucket_size_y); // Flip bucket coordinates vertically

   int         pixel_type;
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
      boost::format itileFormat = tileFormat;
      itileFormat % aov_name;
      itileFormat % tile_size;
      data->client->write(itileFormat.str());
      data->client->write(Message(pixels, tile_size));
      delete pixels;
   }
}


driver_close
{
   AiMsgInfo("[rvdriver] driver close");

   ShaderData *data = (ShaderData*)AiDriverGetLocalData(node);
   if (data->media_name == NULL)
      return;
}

node_finish
{
   AiMsgInfo("[rvdriver] driver finish");
   // release the driver

   ShaderData *data = (ShaderData*)AiDriverGetLocalData(node);

   // we do everything in finish, because close is called multiple times during IPR
   if (data->media_name != NULL)
   {
      delete data->media_name;

      data->client->write("MESSAGE 10 DISCONNECT");
      data->client->close();

      data->io_service->stop();
      AiThreadWait(data->thread);
      AiThreadClose(data->thread);
   }

   delete data->client;
   delete data->work;
   delete data->io_service;

   AiFree(data);
   AiDriverDestroy(node);
}

node_loader
{
   sprintf(node->version, AI_VERSION);

   switch (i)
   {
      case 0:
         node->methods      = (AtNodeMethods*) RVDriverMtd;
         node->output_type  = AI_TYPE_RGBA;
         node->name         = "driver_rv";
         node->node_type    = AI_NODE_DRIVER;
         break;
      default:
      return false;
   }
   return true;
}
