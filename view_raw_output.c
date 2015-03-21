#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <math.h>

// fun thing to test! what happens if pixels at random
// are blurred?!?!
// bugs: if the height is too large, the window borders disappear

typedef unsigned char uchar;

typedef struct {
    uchar luma, chroma;
    uchar blank, hsync, vsync;
} grid_point;

int read_video (uchar ycbcr[], int len, FILE *data_fd);
void wrap (grid_point *grid, int width, int len, uchar ycbcr[]);
void draw (grid_point *grid, int dump_width, int dump_height, GdkPixbuf *pixels, int draw_syncs, int full_range, int colours, int interpolation);
void fail (char format[], ...);
int bit (uchar byte, int n);
int is_new_hsync (uchar old, uchar new);
uchar interpolate (grid_point *point, int y, int width, int height, int interpolation);
float clamp (float value);
void blur (GdkPixbuf *pixels);
void dump (GdkPixbuf *pixels, char *filename);

int hs_flag = 4;
int vs_flag = 5;
int offset;     // i is offset this much from the start of the file

enum colour_types { EIGHT, TWOFIVESIX, ALL };
enum interpolation_types { LINEAR, BILINEAR };
enum do_fuzzing { NO, YES };

int main (int argc, char *argv[])
{
    FILE *data_fd;
    // unsigned so the ycbcr to RGB conversions don't mess up
    uchar *ycbcr;
    int len;    // length of contiguous ycbcr segment returned by read_video
    GtkWidget *window;
    GdkPixbuf *pixels;
    grid_point *grid;
    GtkWidget *image;
    int total_width, video_width;
    int height;
    int draw_syncs = 0;
    int full_range = 1;
    int colours = ALL;
    int interpolation = LINEAR;
    int fuzzing = NO;
    char *dump_filename = NULL;

    gtk_init (&argc, &argv);

    {
        char *filename = NULL;
        int i;
        struct stat stats;
        int data_len;

        if (argc < 2) {
            printf ("usage:\t%s <switches> <file>\n"
                    "\t[-s] show sync regions\n"
                    "\t[-l] use limited-range ycbcr to RGB conversion\n"
                    "\t[-3] 3-bit colour to match S3 Board output\n"
                    "\t[-8] 8-bit colour to match Nexys 3 output\n"
                    "\t[-b] use bilinear interpolation (instead of linear) for other chroma\n"
                    "\t[-f] do a simple fuzzing\n"
                    "\t[-d <filename>] dump the framebuffer to a raw file for verification\n",
                    argv[0]);
            fail ("please specify a file\n");
        }

        for (i = 1; i < argc; i++) {
            if ((argv[i][0] == '-') &&
                    (argv[i][1] == 's'))
                draw_syncs = 1;
            else if ((argv[i][0] == '-') &&
                     (argv[i][1] == 'l'))
                full_range = 0;
            else if ((argv[i][0] == '-') &&
                     (argv[i][1] == '3'))
                colours = EIGHT;
            else if ((argv[i][0] == '-') &&
                     (argv[i][1] == '8'))
                colours = TWOFIVESIX;
           else if ((argv[i][0] == '-') &&
                     (argv[i][1] == 'b'))
                interpolation = BILINEAR;
            else if ((argv[i][0] == '-') &&
                     (argv[i][1] == 'f'))
                fuzzing = YES;
            else if ((argv[i][0] == '-') &&
                     (argv[i][1] == 'd')) {
                dump_filename = argv[i+1];
                i++;
            } else if (((argv[i][0] == '-') &&
                      (argv[i][1] == 'h')) ||
                     (strcmp (argv[i], "--help") == 0))
                fail ("run me with no options to see usage :3\n");
            else if (argv[i][0] == '-')
                fail ("weird switch: %s\n", argv[i]);
            else
                filename = argv[i];
        }

        if (filename == NULL)
            fail ("you didn't specify a file! >:(\n");
        
        data_fd = fopen (filename, "r");
        if (data_fd == NULL)
            fail ("failed to open file: %s\n", strerror (errno));
        if (stat (filename, &stats) == -1)
            fail ("failed to get file length: %s\n", strerror (errno));
        data_len = (int) stats.st_size;

        ycbcr = malloc (data_len);
        len = read_video (ycbcr, data_len, data_fd);
        if (len%2 == 1)
            fprintf (stderr, "warning: length of ycbcr read is not even\n");

        fclose (data_fd);
    }

    // figure out lengths
    {
        int i;
        uchar prev_flags = 0;
        int tot_x = 0, vid_x = 0;

        total_width = 0;
        video_width = 0;
        height = 0;

        for (i = 0; i < len-1; i += 2) {
            if (ycbcr[i] == 0x00) {
                if (is_new_hsync (prev_flags, ycbcr[i+1])) {
                    height++;
                    tot_x = 0;
                    vid_x = 0;
                } else
                    tot_x++;

                prev_flags = ycbcr[i+1];
            } else {
                tot_x++;
                vid_x++;
            }

            if (vid_x > video_width)
                video_width = vid_x;
            if (tot_x > total_width)
                total_width = tot_x;
        }

        printf ("total line length: %d\n", total_width);
        printf ("video width: %d\n", video_width);
        printf ("height: %d\n", height);
    }

    grid = malloc ( (total_width*height) * sizeof(grid_point) );
    if (grid == NULL)
        fail ("couldn't create grid buffer\n");
    // first write to a two-dimensional array so bilinear
    // ycbcr interpolation can be done if needed
    wrap (grid, total_width, len, ycbcr);

    pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                             FALSE,        // has_alpha
                             8,            // bit_per_sample
                             (draw_syncs ? total_width : video_width),
                             height);
    if (pixels == NULL)
        fail ("couldn't create pixel buffer\n");
    draw (grid, total_width, height, pixels, draw_syncs, full_range, colours, interpolation);

    if (dump_filename != NULL) {
        dump (pixels, dump_filename);
        return 0;
    }

    if (fuzzing == YES)
        blur (pixels);

    image = gtk_image_new_from_pixbuf (pixels);
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

    gtk_container_add (GTK_CONTAINER (window), image);

    g_signal_connect (window, "delete-event",
                      G_CALLBACK (exit), 0);
    gtk_widget_show_all (window);
    gtk_main ();

    return 0;
}

// returns the (contiguous video data) length of ycbcr
int read_video (uchar ycbcr[], int len, FILE *data_fd)
{
    int i;
    uchar bytes[2];
    uchar prev_flags = 0x00;    // assume the byte before had hsync active
                                // in case we start in the middle of an hsync pulse
    int last_hsync = -1;

    // skip until blanking period
    for (i = 0;
            (fread (bytes, 1, 1, data_fd) == 1) &&
            (bytes[0] != 0x00);
            i++)
        ;
    fseek (data_fd, -1, SEEK_CUR);
    // skip until new line
    for (; fread (bytes, 1, 2, data_fd) == 2; i+=2) {
        if (bytes[0] == 0x00) {
            if (is_new_hsync (prev_flags, bytes[1]))
                break;
            prev_flags = bytes[1];
        }
    }
    if (feof (data_fd))
        fail ("data too short\n");
    fseek (data_fd, -2, SEEK_CUR);

    offset = i;    // so positions in the file can be debugged
    printf ("first line starts at 0x%02x\n", i);
    len -= i;
    fread (ycbcr, len, 1, data_fd);

    // cut off last incomplete line
    for (i = 0; i < len-1; i+=2) {
        if (ycbcr[i] == 0x00) {
            if (is_new_hsync (prev_flags, ycbcr[i+1]))
                last_hsync = i;
            prev_flags = ycbcr[i+1];
        }
    }
    if (last_hsync == -1)
        fail ("couldn't find any hsyncs in the data\n");
    // +2 so that the hsync flag gets picked up
    // otherwise the last line won't get counted
    // when calculating the height
    len = last_hsync+2;
    printf ("last line ends at 0x%02x\n", offset+len);

    return len;
}

// process the ycbcr into a grid
void wrap (grid_point *grid, int width, int len, uchar *ycbcr)
{
    int i, x, y;
    uchar prev_flags = 0x00;
    grid_point *point;

    for (i = 0, x = 0, y = 0;
            i < len-1;    // max reached ahead is 1 byte
            i += 2) {

        if (ycbcr[i] == 0x00) {
            if (is_new_hsync (prev_flags, ycbcr[i+1])) {
                x = 0;
                y++;
            }

            prev_flags = ycbcr[i+1];
        }

        point = grid + y*width + x;

        if (ycbcr[i] == 0x00) {
            point->luma = 0;
            point->chroma = 0;
            point->blank = 1;
            // these are active-low in the data
            point->hsync = ~ bit (ycbcr[i+1], hs_flag);
            point->vsync = ~ bit (ycbcr[i+1], vs_flag);
        } else {
            point->luma = ycbcr[i];
            point->chroma = ycbcr[i+1];
            point->blank = 0;
            point->hsync = 0;
            point->vsync = 0;
        }

        x++;
    }

}

// convert ycbcr to rgb and draw it
void draw (grid_point *grid, int dump_width, int dump_height, GdkPixbuf *pixels, int draw_syncs, int full_range, int colours, int interpolation)
{
    // two different x variables needed because if syncs aren't being drawn
    // the target x will be different from the line x
    int line_x, vid_x, y;
    uchar Y, Cb, Cr;
    float sR, sG, sB;
    uchar R, G, B;
    uchar *rgb;
    int rowstride;    // distance between line starts in pixbuf
    int n_channels;    // bytes per pixel
    uchar *target;
    grid_point *point;

    rgb = gdk_pixbuf_get_pixels (pixels);
    rowstride = gdk_pixbuf_get_rowstride (pixels);
    n_channels = gdk_pixbuf_get_n_channels (pixels);

    line_x = 0;
    vid_x = 0;
    y = 0;

    for (y = 0; y < dump_height; y++) {
        for (line_x = 0, vid_x = 0; line_x < dump_width; line_x++) {
            R = 0;
            G = 0;
            B = 0;

            point = grid + y*dump_width + line_x;
            if (draw_syncs)
                target = rgb + y*rowstride + line_x*n_channels;
            else
                target = rgb + y*rowstride + vid_x*n_channels;

            if (point->blank && draw_syncs) {
                R = 0;
                G = 50 + point->hsync*100;
                B = 50 + point->vsync*100;
            } else if (!point->blank) {
                Y = point->luma;
                if (vid_x % 2 == 0) {
                    Cb = point->chroma;
                    Cr = interpolate (point, y, dump_width, dump_height, interpolation);
                } else {
                    Cb = interpolate (point, y, dump_width, dump_height, interpolation);
                    Cr = point->chroma;
                }

                if (full_range == 0) {
                    sR = Y + 1.371*(Cr-128);
                    sG = Y - 0.698*(Cr-128) - 0.336*(Cb-128);
                    sB = Y + 1.732*(Cb-128);
                } else if (full_range == 1) {
                    sR = 1.164*(Y-16) + 1.596*(Cr-128);
                    sG = 1.164*(Y-16) - 0.813*(Cr-128) - 0.392*(Cb-128);
                    sB = 1.164*(Y-16) + 2.017*(Cb-128);
                }
                // the calculation breaks a bit sometimes, so clamp it
                // to 0-255 so no underflow/overflow occurs
                R = (uchar) round(clamp(sR));
                G = (uchar) round(clamp(sG));
                B = (uchar) round(clamp(sB));

                if (colours == EIGHT) {
                    R &= (0b10000000);
                    G &= (0b10000000);
                    B &= (0b10000000);
                    R = (R >= 128) ? 255 : 0;
                    G = (G >= 128) ? 255 : 0;
                    B = (B >= 128) ? 255 : 0;
                } else if (colours == TWOFIVESIX) {
                    R &= 0b11100000;
                    R >>= 5;
                    R = (uchar) round((R / 7.0) * 255.0);
                    G &= 0b11100000;
                    G >>= 5;
                    G = (uchar) round((G / 7.0) * 255.0);
                    B &= 0b11000000;
                    B >>= 6;
                    B = (uchar) round((B / 3.0) * 255.0);
                }

                vid_x++;
            }
            
            target[0] = R;
            target[1] = G;
            target[2] = B;
        }
    }
}

void fail (char format[], ...)
{
    va_list why;
    va_start (why, format);
    vfprintf (stderr, format, why);
    va_end (why);
    exit (1);
}

int bit (uchar byte, int n)
{
    return (byte >> n) & 1;
}

int is_new_hsync (uchar old, uchar new)
{
    return !bit (new, hs_flag) && bit (old, hs_flag);
}

uchar interpolate (grid_point *point, int y, int width, int height, int interpolation)
{
    float chroma = 0;
    int values = 0;

    // video is always surrounding by blanking, so no worries
    // here about point-1 or point+1
    if (!(point-1)->blank) {
        chroma += (float) (point-1)->chroma;
        values++;
    } if (!(point+1)->blank) {
        chroma += (float) (point+1)->chroma;
        values++;
    }

    if (interpolation == BILINEAR) {
        // a line doesn't necessarily have one above or below it, though,
        // so care is needed here
        if (y != 0) {
            chroma += interpolate (point-width, y, width, height, LINEAR);
            values++;
        }
        if (y != height) {
            chroma += interpolate (point+width, y, width, height, LINEAR);
            values++;
        }
    }

    chroma /= values;

    return (uchar) round (chroma);
}

float clamp (float value)
{
    if (value > 255)
        return 255;
    else if (value < 0)
        return 0;
    else return value;
}

void add_colour (int dest[3], uchar src[3])
{
    dest[0] += src[0];
    dest[1] += src[1];
    dest[2] += src[2];
}

void divide_colour (int dest[3], int k)
{
    float kf = (float) k;
    float d0 = dest[0] / kf;
    float d1 = dest[1] / kf;
    float d2 = dest[2] / kf;

    dest[0] = (int) round (d0);
    dest[1] = (int) round (d1);
    dest[2] = (int) round (d2);
}

void blur (GdkPixbuf *pixels)
{
    uchar *rgb_s, *rgb_d;
    int rowstride;
    int n_channels;
    uchar *target, *source;
    int width, height;
    int x, y;
    int values;
    int colour[3];

    rowstride = gdk_pixbuf_get_rowstride (pixels);
    n_channels = gdk_pixbuf_get_n_channels (pixels);
    width = gdk_pixbuf_get_width (pixels);
    height = gdk_pixbuf_get_height (pixels);

    rgb_d = gdk_pixbuf_get_pixels (pixels);
    rgb_s = malloc (height*rowstride);
    memcpy (rgb_s, rgb_d, height*rowstride); // copy pixels from the original to the backup

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            source = rgb_s + y*rowstride + x*n_channels;
            target = rgb_d + y*rowstride + x*n_channels;

            memset (colour, 0, sizeof(colour));
            /*add_colour (colour, source);
            values = 1;*/
            values = 0;

            if (x != 0) {
                add_colour (colour, source-n_channels);
                values++;
            }
            if (x != width-1) {
                add_colour (colour, source+n_channels);
                values++;
            }
            if (y != 0) {
                add_colour (colour, source-rowstride);
                values++;
            }
            if (y != height-1) {
                add_colour (colour, source+rowstride);
                values++;
            }

            if (x != 0 && y != 0) {
                add_colour (colour, source-n_channels-rowstride);
                values++;
            }
            if (x != width-1 && y != height-1) {
                add_colour (colour, source+n_channels+rowstride);
                values++;
            }
            if (x != width-1 && y != 0) {
                add_colour (colour, source+n_channels-rowstride);
                values++;
            }
            if (x != 0 && y != height-1) {
                add_colour (colour, source-n_channels+rowstride);
                values++;
            }

            divide_colour (colour, values);
            target[0] = colour[0];
            target[1] = colour[1];
            target[2] = colour[2];
        }
    }
}

void dump (GdkPixbuf *pixels, char *filename)
{
    FILE *fd;
    int x, y;
    int width, height;
    int rowstride, n_channels;
    uchar *rgb, *pixel;

    fd = fopen (filename, "w");
    if (fd == NULL)
        fail ("couldn't open dump file: %s\n", strerror(errno));
    width = gdk_pixbuf_get_width (pixels);
    height = gdk_pixbuf_get_height (pixels);
    rowstride = gdk_pixbuf_get_rowstride (pixels);
    n_channels = gdk_pixbuf_get_n_channels (pixels);
    rgb = gdk_pixbuf_get_pixels (pixels);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            pixel = rgb + y*rowstride + x*n_channels;
            fprintf (fd, "%02x%02x%02x", pixel[0], pixel[1], pixel[2]);
        }
        fprintf (fd, "\n");
    }

    fclose (fd);
}
