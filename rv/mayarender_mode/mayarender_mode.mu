
module: mayarender_mode {

require rvtypes;
require commands;
require extra_commands;
require rvui;
require gl;
require system;
require qt;
require io;
require runtime;

class: MayaRenderMode : rvtypes.MinorMode
{
   bool _debug;
   bool _paused;
   float _startx;
   float _starty;
   float _endx;
   float _endy;
   qt.QDialog _setupDlg;
   qt.QLineEdit _hostWidget;
   qt.QLineEdit _portWidget;
   string _iprCmdStart;
   string _iprCmdElse;
   string _iprCmdEnd;
   string _host;
   int _port;
   bool _win;

   method: deb (void; string msg)
   {
      if (_debug == true)
      {
         print("[Maya Render] %s\n" % msg);
      }
   }

   method: sendMayaCommand(void; string cmd)
   {
      deb("Send command: %s" % cmd);
      string sendcmd = "sendmaya -h %s -p %d " % (_host, _port);
      if (_debug)
      {
         sendcmd += "-v ";
      }
      sendcmd += "-- %s" % cmd;
      system.system(sendcmd);
   }
   
   method: singleRender (void; Event e)
   {
      sendMayaCommand("\"renderWindowRender redoPreviousRender renderView;\"");
   }

   method: singleRenderRegion (void; Event e)
   {
      sendMayaCommand("\"renderWindowRenderRegion renderView;\"");
   }

   method: singleRenderCurrent (void; Event e)
   {
      if (_win)
      {
         sendMayaCommand("\"string $p = `playblast -activeEditor`; string $c = `modelPanel -query -camera (substitute(\\\".*^^^|\\\", $p, \\\"\\\"))`; renderWindowRenderCamera render renderView $c;\"");
      }
      else
      {
         sendMayaCommand("\"string \\$p = \\`playblast -activeEditor\\`; string \\$c = \\`modelPanel -query -camera (substitute(\\\".*|\\\", \\$p, \\\"\\\"))\\`; renderWindowRenderCamera render renderView \\$c;\"");
      }
   }

   method: iprStart (void; Event e)
   {
      sendMayaCommand("\"renderWindowRender(\\\"redoPreviousIprRender\\\", \\\"\\\");\"");
   }

   method: iprStartCurrent (void; Event e)
   {
      if (_win)
      {
         sendMayaCommand("\"string $p = `playblast -activeEditor`; string $c = `modelPanel -query -camera (substitute(\\\".*^^^|\\\", $p, \\\"\\\"))`; renderWindowRenderCamera iprRender renderView $c;\"");
      }
      else
      {
         sendMayaCommand("\"string \\$p = \\`playblast -activeEditor\\`; string \\$c = \\`modelPanel -query -camera (substitute(\\\".*|\\\", \\$p, \\\"\\\"))\\`; renderWindowRenderCamera iprRender renderView \\$c;\"");
      }
   }

   method: iprPause (void; Event e)
   {
      _paused = !_paused;
      sendMayaCommand(_iprCmdStart + "renderViewTogglePauseIpr renderView;" + _iprCmdEnd);
   }

   method: iprPausedState (int; )
   {
      return if (_paused == true) then commands.CheckedMenuState else commands.UncheckedMenuState;
   }

   method: iprRefresh (void; Event e)
   {
      sendMayaCommand(_iprCmdStart + "refreshIprImage;" + _iprCmdEnd);
   }

   method: iprStop (void; Event e)
   {
      sendMayaCommand("\"stopIprRendering renderView;\"");
   }

   method: updateMayaSetup (void; )
   {
      _host = _hostWidget.text();
      _port = int(_portWidget.text());
   }

   method: setupMaya (void; Event e)
   {
      if (_setupDlg eq nil)
      {
         string uipath = io.path.join(supportPath("mayarender_mode", "mayarender"), "maya_command_port.ui");
         if (!io.path.exists(uipath))
         {
            uipath = "maya_command_port.ui";
            if (!io.path.exists(uipath))
            {
               print("Cannot find \"maya_command_port.ui\" file\n");
               return;
            }
         }

         let m = commands.mainWindowWidget();
         _setupDlg = qt.loadUIFile(uipath, m);
         _hostWidget = _setupDlg.findChild("hostEdit");
         _portWidget = _setupDlg.findChild("portEdit");

         qt.connect(_setupDlg, qt.QDialog.accepted, updateMayaSetup);
      }

      _hostWidget.setText(_host);
      _portWidget.setText("%d" % _port);
      _setupDlg.show();
   }

   method: buildMenu (rvtypes.Menu; )
   {
      rvtypes.Menu m1 = rvtypes.Menu {
         {"Render Current", singleRenderCurrent, nil, nil},
         {"Re-Render", singleRender, nil, nil},
         {"Render Region", singleRenderRegion, nil, nil},
         {"_", nil},
         {"IPR Render Current", iprStartCurrent, nil, nil},
         {"IPR Re-Render", iprStart, nil, nil},
         {"IPR Pause", iprPause, nil, iprPausedState},
         {"IPR Refresh", iprRefresh, nil, nil},
         {"IPR Stop", iprStop, nil, nil},
         {"_", nil},
         {"Configure...", setupMaya, nil, nil}
      };

      rvtypes.Menu ml = rvtypes.Menu {
         {"Maya Render", m1}
      };

      return ml;
   }

   method: startRegion (void; Event event)
   {
      _startx = event.pointer().x;
      _starty = event.pointer().y;
      _endx = -1.0;
      _endy = -1.0;
      
      deb("Start region from (%f, %f)" % (_startx, _starty));
      
      commands.redraw();
      
      event.reject();
   }

   method: growRegion (void; Event event)
   {
      if (_startx >= 0.0 && _starty >= 0.0)
      {
         _endx = event.pointer().x;
         _endy = event.pointer().y;
         deb("Grow region to (%f, %f)" % (_endx, _endy));

         commands.redraw();
      }

      event.reject();
   }

   method: resetRegion(void; )
   {
      _startx = -1.0;
      _starty = -1.0;
      _endx = -1.0;
      _endy = -1.0;

      // just reseting render region doesn't work, also need to reset marquee
      
      string cmd;

      if (_win)
      {
         cmd = "renderWindowEditor -e -mq 1 0 0 1 renderView; renderWindowEditor -e -rr renderView; string $cmd = `renderer -q -changeIprRegionProcedure (currentRenderer())`; eval $cmd renderView;";
      }
      else
      {
         cmd = "renderWindowEditor -e -mq 1 0 0 1 renderView; renderWindowEditor -e -rr renderView; string \\$cmd = \\`renderer -q -changeIprRegionProcedure (currentRenderer())\\`; eval \\$cmd renderView;";
      }
      
      sendMayaCommand(_iprCmdStart + cmd + _iprCmdEnd);

      commands.redraw();
   }

   method: endRegion (void; Event event)
   {
      deb("End region");

      _endx = event.pointer().x;
      _endy = event.pointer().y;

      PixelImageInfo[] pinf = commands.sourceAtPixel(event.pointer());
      if (pinf.size() == 0)
      {
         resetRegion();
         event.reject();
         return;
      }
      
      (vector float[2])[] geom = commands.imageGeometry(pinf[0].name);
      // [0] bottom-left
      // [1] bottom-right
      // [2] top-right
      // [3] top-left

      float img_bottom = geom[0][1];
      float img_top = geom[2][1];
      float img_left = geom[0][0];
      float img_right = geom[1][0];
      float img_inv_aspect;

      if (img_bottom >= img_top || img_left >= img_right)
      {
         resetRegion();
         event.reject();
         return;
      }
      img_inv_aspect = (img_top  - img_bottom) / (img_right - img_left);

      float left;
      float right;
      float top;
      float bottom;

      if (_startx < _endx)
      {
         left = if _startx < img_left then img_left else _startx;
         right = if _endx > img_right then img_right else _endx;
      }
      else
      {
         left = if _endx < img_left then img_left else _endx;
         right = if _startx > img_right then img_right else _startx;
      }

      if (_starty < _endy)
      {
         bottom = if _starty < img_bottom then img_bottom else _starty;
         top = if _endy > img_top then img_top else _endy;
      }
      else
      {
         bottom = if _endy < img_bottom then img_bottom else _endy;
         top = if _starty > img_top then img_top else _starty;
      }

      if (bottom >= top || left >= right)
      {
         resetRegion();
         event.reject();
         return;
      }

      // normalize coords
      top = (top - img_bottom) / (img_top - img_bottom);
      bottom = (bottom - img_bottom) / (img_top - img_bottom);
      left = (left - img_left) / (img_right - img_left);
      right = (right - img_left) / (img_right - img_left);

      deb("Normalized area (%f, %f) -> (%f, %f)" % (bottom, left, top, right));

      // remap top and bottom range from [0, 1] to [0, 1 / image aspect ratio]
      top = top * img_inv_aspect;
      bottom = bottom * img_inv_aspect;

      string cmd0;

      if (_win)
      {
         cmd0 = "renderWindowEditor -e -mq %f %f %f %f renderView; string $cmd = `renderer -q -changeIprRegionProcedure (currentRenderer())`; eval $cmd renderView;" % (top, left, bottom, right);
      }
      else
      {
         cmd0 = "renderWindowEditor -e -mq %f %f %f %f renderView; string \\$cmd = \\`renderer -q -changeIprRegionProcedure (currentRenderer())\\`; eval \\$cmd renderView;" % (top, left, bottom, right);
      }

      string cmd1 = "renderWindowCheckAndRenderRegion %f %f %f %f;" % (top, left, bottom, right);
      
      sendMayaCommand(_iprCmdStart + cmd0 + _iprCmdElse + cmd1 + _iprCmdEnd);
      
      commands.redraw();
      event.reject();
   }

   method: displayRegion (void; Event event)
   {
      if (_startx >= 0 && _starty >= 0 && _endx >= 0 && _endy >= 0)
      {
         rvui.setupProjection(event);

         gl.glColor(1.0, 0.0, 0.0);
         gl.glBegin(gl.GL_LINE_STRIP);
         gl.glVertex(_startx, _starty);
         gl.glVertex(_endx, _starty);
         gl.glVertex(_endx, _endy);
         gl.glVertex(_startx, _endy);
         gl.glVertex(_startx, _starty);
         gl.glEnd();
      }
      
      event.reject();
   }

   method: MayaRenderMode(MayaRenderMode; string name)
   {
      init(name,
           nil,
           [("pointer-2--shift--push", startRegion, ""),
            ("pointer-2--shift--drag", growRegion, ""),
            ("pointer-2--shift--release", endRegion, ""),
            ("render", displayRegion, "")],
           buildMenu() 
      );

      _paused = false;
      _startx = -1.0;
      _starty = -1.0;
      _endx = -1.0;
      _endy = -1.0;
      _debug = false;
      _host = "localhost";
      _port = 4700;
      _win = (runtime.build_os() == "WINDOWS");

      // on windows: escape the following characters with ^: \ & | > < ^
      // on unix   : escape the following characters with \: ` $

      if (_win)
      {
         _iprCmdStart = "\"string $isri = `renderer -q -isr (currentRenderer())`; if (size($isri) ^^^> 0 ^^^&^^^& eval($isri)) { ";
      }
      else
      {
         _iprCmdStart = "\"string \\$isri = \\`renderer -q -isr (currentRenderer())\\`; if (size(\\$isri) > 0 && eval(\\$isri)) { ";
      }
      _iprCmdElse = " } else { ";
      _iprCmdEnd = " }\"";
   }
}

\: createMode (rvtypes.Mode;)
{
   print("Load mayarender\n");
   return MayaRenderMode("mayarender_mode");
}

\: theMode (MayaRenderMode; )
{
   MayaRenderMode m = rvui.minorModeFromName("mayarender_mode");
   return m;
}

}

