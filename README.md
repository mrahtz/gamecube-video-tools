A few tools for viewing raw output from the GameCube's [Digital AV
port](http://gamesx.com/wiki/doku.php?id=av:nintendodigitalav).

* `view_raw_output.c`: a GTK+ utility to view the dumps.

  Needs GTK+ 2.0 development libraries installed (Ubuntu: `libgtk2.0-dev`).
  
  Run with no options to see arguments. Examples:

  ```
  $ ./view_raw_output video_dumps/zelda_interlaced_1.bin
  ```
  ![zelda_interlaced_1.bin](/example_images/zelda_interlaced_1.png)
  ```
  $ ./view_raw_output video_dumps/zelda_progressive_2.bin
  ```
  ![zelda_progressive_2.bin](/example_images/zelda_progressive_2.png)
  ```
  # -s: show sync regions
  $ ./view_raw_output -s video_dumps/menu.bin 
  ```
  ![menu.bin](/example_images/menu_with_sync.png)

