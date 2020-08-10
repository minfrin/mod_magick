# mod_magick

The Apache mod\_magick module provides image filtering for the Apache
httpd server.

- Download:

RPM Packages are available at
[COPR](https://copr.fedorainfracloud.org/coprs/minfrin/mod_magick/) for EPEL,
Fedora and OpenSUSE.

```
dnf copr enable minfrin/mod_magick
dnf install mod_magick
```

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK
    AddMagickOption jpeg:preserve-settings true
  </If>
</FilesMatch>
```

The *MAGICK* filter converts a response into a magick bucket, which can be
transformed by specific downstream magick filters to modify the image.
The first filter that attempts to read the bucket will cause the output
image to be rendered.

The *AddMagickOption* allows the setting of options that affect the
operation of GraphicsMagick. The options accepted are those as documented
under the -define option in the gm tool.

The *MagickMaxSize* option sets the largest size the source image is allowed to
be. Beyond this size requests will be rejected to prevent the processing of
huge images.

- Examples:

In this example, we generate thumbnails if the width is added to the query
string.

We also take into account HTTP Client Hints provided in the DPR, Width and
Save-Data headers.

```
  <FilesMatch ".+\.(gif|jpe?g|png)$">
    AddMagickOption jpeg:preserve-settings true
    MagickResizeModulus 20
    MagickInterlace plane

    <If "%{QUERY_STRING} in { '200', '400', '800', '1600', '3200' }">
      SetOutputFilter MAGICK;MAGICK_RESIZE;MAGICK_STRIP;MAGICK_INTERLACE

      <If "%{req:Width} != ''">
        MagickResizeColumns %{req:Width}
      </If>
      MagickResizeColumns %{QUERY_STRING}

      <If "%{req:DPR} != ''">
        MagickResizeFactor %{req:DPR}
      </If>

      <If "%{req:Save-Data} == 'on'">
        MagickResizeFactor 0.5
      </If>

    </If>
  </FilesMatch>
```

# mod\_magick\_colorspace

The Apache mod\_magick\_colorspace module provides a filter that sets the
colorspace type of the image generated by mod\_magick.

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK_COLORSPACE
    MagickColorspace srgb
  </If>
</FilesMatch>
```

The *MagickColorspace* directive sets the colorspace to be used by the
output image.

Possible values are:

```
cmyk|gray|hsl|hwb|ohta|rgb|srgb|transparent|xyz|ycbcr|ycc|yiq|ypbpr|yuv
```

The default value is srgb.

# mod\_magick\_format

The Apache mod\_magick\_format module provides a filter that sets the
output format of an image read by mod\_magick.

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK;MAGICK_FORMAT
    MagickFormat PNG
  </If>
</FilesMatch>
```

The *MagickFormat* directive sets the output format to be used. The list
of supported formats can be found in the manual of the GraphicsMagick
'gm' command.

# mod\_magick\_interlace

The Apache mod\_magick\_interlace module provides a filter that sets the
interlace type of the image generated by mod\_magick.

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK;MAGICK_INTERLACE
    MagickInterlace plane
  </If>
</FilesMatch>
```

The *MagickInterlace* directive takes an expression containing the interlace
type to be used. Possible options are: none|line|plane|partition

# mod\_magick\_quality

The Apache mod\_magick\_quality module provides a filter that sets the
quality of the output image processed by mod\_magick.

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK_QUALITY
    MagickQuality 82
  </If>
</FilesMatch>
```

The *MagickQuality* directive provides an expression that sets the
quality of the output image.

In the case of JPEG images, the original quality level can be preserved
by setting the following option:

```
  AddMagickOption jpeg:preserve-settings true
```

# mod\_magick\_resize

The Apache mod\_magick\_resize module provides a filter that resizes an
image read by mod\_magick.

- Basic configuration:

```
  <FilesMatch ".+\.(gif|jpe?g|png)$">
    <If "%{QUERY_STRING} =~ /./">
      SetOutputFilter MAGICK;MAGICK_RESIZE
      MagickResizeColumns %{QUERY_STRING}
    </If>
  </FilesMatch>
```

In the above configuration, if we see any filenames ending in the major
graphics formats, and if a query string is present containing the desired
width of the image, we add the magick and magick resize filters, and
request that the width be set to the value of the query string, falling
back to the original width if the query string could not be interpreted as
a valid number.

To protect against denial of service, dimensions of the image are
automatically capped at the dimensions of the original image. The browser
will typically do any resizing up required.

All resize directives take on a list of expressions, the last expression
to return a valid value wins. Any expression that renders an invalid value
(such as the empty string) is skipped and the previous expression is tried.
This allows support for responsive behaviour such as HTTP Client Hints.

For example, here we first take into account the HTTP Client Hint "Width"
header, followed by the query string, followed by the default fallback value
"100".

```
  SetOutputFilter MAGICK;MAGICK_RESIZE
  MagickResizeColumns 100 %{QUERY_STRING}
  <If "%{req:Width} != ''">
    MagickResizeColumns %{req:Width}
  </If>
```

Note that in the above example, we need to include the If section to ensure
the Vary header is has the Width header correctly added. Apache httpd 2.4
has a bug where conditional expressions set the Vary header, but string
expressions do not. Without this, caching breaks, and you want caching.

In the absence of the above bug, the above line should look like this:

```
  #MagickResizeColumns 100 %{QUERY_STRING} %{req:Width}
```

In the absence of a valid fallback value, or if the fallback value is zero,
the original image value is maintained.

The *MagickResizeColumns* and *MagickResizeRows* directives control the width
and height of the resulting image. If either the width or height is left
unspecified it will be calculated based on the aspect ratio of the original
image.

The *MagickResizeFilterType* directive specifies the filter to use while
resizing. Possible values are:

```
  MagickResizeFilterType bessel|blackman|box|catrom|
     cubic|gaussian|hamming|hanning|hermite|lanczos|mitchell|point|
     quadratic|sinc|triangle
```

The *MagickResizeBlur* directive specifies the amount of blur to add while
resizing, where greater than 1 is blurry, and less than 1 is sharp. The
default is 1.

The *MagickResizeFactor* directive can be used to apply a multiplier to the
width and height. For example, to use the HTTP Client Hint Save-Data header,
falling back to the DPR header, falling back to the default factor of 1:

```
  SetOutputFilter MAGICK;MAGICK_RESIZE
  MagickResizeWidth 400
  <If "%{req:DPR} != ''">
    # If the DPR is 2, the resulting width will be 800
    MagickResizeFactor %{req:DPR}
  </If>
  <If "%{req:Save-Data} = 'on'">
    # If the Save-Data is on, the resulting width will be 200
    MagickResizeFactor 0.5
  </If>
```

The *MagickResizeModulus* directive limits the possible image sizes so that
caches are not overwhelmed. All requested rows and columns are rounded up to
the nearest modulus value. Any calculated rows and columns value will be left
unchanged to keep the aspect ratio.

```
  # Resulting width will be 300
  MagickResizeWidth 201
  MagickResizeModulus 100
```

# mod\_magick\_strip

The Apache mod\_magick\_strip module provides a filter that strips all
image metadata from an image read by mod\_magick.

- Basic configuration:

```
<FilesMatch ".+\.(gif|jpe?g|png)$">
  <If "%{QUERY_STRING} =~ /./">
    SetOutputFilter MAGICK;MAGICK_STRIP
  </If>
</FilesMatch>
```

The magick strip filter has no directives, when present metadata is
stripped.
