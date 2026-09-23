#include <pappl/pappl.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define HPLP_APP_VERSION "0.1.0"

static pappl_pr_driver_t drivers[] = {
    {
        "hp-laserjet-pro-p1102",
        "HP LaserJet Pro P1102",
        "MFG:Hewlett-Packard;MDL:HP LaserJet Professional P1102;CMD:ZJS,PJL,ACL,HTTP;",
        NULL
    }
};

static void set_media(pappl_media_col_t *media,
                      const char *size_name,
                      const char *source,
                      const char *type,
                      int left_right,
                      int bottom_top)
{
    const pwg_media_t *pwg = pwgMediaForPWG(size_name);

    memset(media, 0, sizeof(*media));

    if (pwg != NULL) {
        media->size_width = pwg->width;
        media->size_length = pwg->length;
    }

    snprintf(media->size_name, sizeof(media->size_name), "%s", size_name);
    snprintf(media->source, sizeof(media->source), "%s", source);
    snprintf(media->type, sizeof(media->type), "%s", type);

    media->left_margin = left_right;
    media->right_margin = left_right;
    media->bottom_margin = bottom_top;
    media->top_margin = bottom_top;
}


static bool disabled_rstartjob(pappl_job_t *job,
                               pappl_pr_options_t *options,
                               pappl_device_t *device)
{
    (void)options;
    (void)device;

    papplLogJob(job,
                PAPPL_LOGLEVEL_ERROR,
                "P1102 raster backend is not connected yet; job stopped safely.");
    return false;
}

static bool disabled_rstartpage(pappl_job_t *job,
                                pappl_pr_options_t *options,
                                pappl_device_t *device,
                                unsigned page)
{
    (void)job;
    (void)options;
    (void)device;
    (void)page;
    return false;
}

static bool disabled_rwriteline(pappl_job_t *job,
                                pappl_pr_options_t *options,
                                pappl_device_t *device,
                                unsigned y,
                                const unsigned char *line)
{
    (void)job;
    (void)options;
    (void)device;
    (void)y;
    (void)line;
    return false;
}

static bool disabled_rendpage(pappl_job_t *job,
                              pappl_pr_options_t *options,
                              pappl_device_t *device,
                              unsigned page)
{
    (void)job;
    (void)options;
    (void)device;
    (void)page;
    return true;
}

static bool disabled_rendjob(pappl_job_t *job,
                             pappl_pr_options_t *options,
                             pappl_device_t *device)
{
    (void)job;
    (void)options;
    (void)device;
    return true;
}

static bool driver_cb(pappl_system_t *system,
                      const char *driver_name,
                      const char *device_uri,
                      const char *device_id,
                      pappl_pr_driver_data_t *driver_data,
                      ipp_t **driver_attrs,
                      void *data)
{
    (void)device_uri;
    (void)device_id;
    (void)data;

    if (driver_name == NULL ||
        driver_data == NULL ||
        driver_attrs == NULL ||
        strcmp(driver_name, "hp-laserjet-pro-p1102") != 0) {
        papplLog(system,
                 PAPPL_LOGLEVEL_ERROR,
                 "Unsupported or incomplete driver request.");
        return false;
    }

    /* PAPPL pre-initializes driver_data with valid defaults. Preserve them. */
    *driver_attrs = NULL;

    snprintf(driver_data->make_and_model,
             sizeof(driver_data->make_and_model),
             "%s",
             "HP LaserJet Pro P1102");

    /*
     * Stage 1 only advertises the real printer capabilities. Raster callbacks
     * and the custom libusb transport are connected in the next milestone.
     */
    driver_data->format = "image/pwg-raster";
    driver_data->kind = PAPPL_KIND_DOCUMENT;
    driver_data->ppm = 18;

    /*
     * PAPPL requires the complete raster callback set before it accepts
     * capability data. These safe stubs deliberately reject a print job at
     * rstartjob until the real P1102 renderer/transport is wired in.
     */
    driver_data->rstartjob_cb = disabled_rstartjob;
    driver_data->rstartpage_cb = disabled_rstartpage;
    driver_data->rwriteline_cb = disabled_rwriteline;
    driver_data->rendpage_cb = disabled_rendpage;
    driver_data->rendjob_cb = disabled_rendjob;

    driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->quality_default = IPP_QUALITY_NORMAL;
    driver_data->orient_default = IPP_ORIENT_NONE;
    driver_data->scaling_default = PAPPL_SCALING_AUTO;

    driver_data->raster_types =
        PAPPL_PWG_RASTER_TYPE_BLACK_1 |
        PAPPL_PWG_RASTER_TYPE_BLACK_8 |
        PAPPL_PWG_RASTER_TYPE_SGRAY_8;

    driver_data->num_resolution = 2;
    driver_data->x_resolution[0] = 600;
    driver_data->y_resolution[0] = 600;
    driver_data->x_resolution[1] = 1200;
    driver_data->y_resolution[1] = 600;
    driver_data->x_default = 600;
    driver_data->y_default = 600;

    driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;
    driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;

    /*
     * P1102 PPD imageable-area margin is about 4 mm.
     * PAPPL stores margins in hundredths of millimeters.
     */
    driver_data->left_right = 400;
    driver_data->bottom_top = 400;

    driver_data->num_media = 5;
    driver_data->media[0] = "iso_a4_210x297mm";
    driver_data->media[1] = "iso_a5_148x210mm";
    driver_data->media[2] = "iso_a6_105x148mm";
    driver_data->media[3] = "na_letter_8.5x11in";
    driver_data->media[4] = "na_legal_8.5x14in";

    driver_data->num_source = 1;
    driver_data->source[0] = "main";

    driver_data->num_type = 1;
    driver_data->type[0] = "stationery";

    set_media(&driver_data->media_default,
              "iso_a4_210x297mm",
              "main",
              "stationery",
              driver_data->left_right,
              driver_data->bottom_top);

    driver_data->media_ready[0] = driver_data->media_default;

    return true;
}

int main(int argc, char *argv[])
{
    return papplMainloop(
        argc,
        argv,
        HPLP_APP_VERSION,
        "HP Legacy Print",
        (int)(sizeof(drivers) / sizeof(drivers[0])),
        drivers,
        NULL,
        driver_cb,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
}
