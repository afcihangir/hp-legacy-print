#include <pappl/pappl.h>

#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define HPLP_APP_VERSION "0.2.0"

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



typedef struct {
    FILE *pbm;
    char pbm_path[64];
    char zjs_path[64];
    int width;
    int height;
    int xres;
    int yres;
    int paper_code;
    int copies;
    int rows_written;
    int pages;
    bool failed;
} p1102_job_data_t;

static int paper_code_for_media(const char *name)
{
    if (!name)
        return 0;
    if (!strcmp(name, "iso_a4_210x297mm"))
        return 9;
    if (!strcmp(name, "iso_a5_148x210mm"))
        return 11;
    if (!strcmp(name, "iso_a6_105x148mm"))
        return 70;
    if (!strcmp(name, "na_letter_8.5x11in"))
        return 1;
    if (!strcmp(name, "na_legal_8.5x14in"))
        return 5;
    return 0;
}

static void cleanup_job_data(p1102_job_data_t *job_data)
{
    if (!job_data)
        return;

    if (job_data->pbm) {
        fclose(job_data->pbm);
        job_data->pbm = NULL;
    }

    if (job_data->pbm_path[0])
        unlink(job_data->pbm_path);
    if (job_data->zjs_path[0])
        unlink(job_data->zjs_path);

    free(job_data);
}

static bool raster_rstartjob(pappl_job_t *job,
                             pappl_pr_options_t *options,
                             pappl_device_t *device)
{
    p1102_job_data_t *job_data;
    int fd;

    (void)options;
    (void)device;

    job_data = calloc(1, sizeof(*job_data));
    if (!job_data) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Unable to allocate render state.");
        return false;
    }

    snprintf(job_data->pbm_path, sizeof(job_data->pbm_path),
             "/tmp/hplp-p1102-XXXXXX");
    fd = mkstemp(job_data->pbm_path);
    if (fd < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to create temporary PBM file.");
        free(job_data);
        return false;
    }

    job_data->pbm = fdopen(fd, "wb");
    if (!job_data->pbm) {
        close(fd);
        unlink(job_data->pbm_path);
        free(job_data);
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to open temporary PBM stream.");
        return false;
    }

    papplJobSetData(job, job_data);
    return true;
}

static bool raster_rstartpage(pappl_job_t *job,
                              pappl_pr_options_t *options,
                              pappl_device_t *device,
                              unsigned page)
{
    p1102_job_data_t *job_data =
        (p1102_job_data_t *)papplJobGetData(job);
    int paper_code;

    (void)device;
    (void)page;

    if (!job_data || !job_data->pbm)
        return false;

    if (options->header.cupsBitsPerPixel != 1) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Expected 1-bit monochrome raster, got %u bits/pixel.",
                    options->header.cupsBitsPerPixel);
        job_data->failed = true;
        return false;
    }

    paper_code = paper_code_for_media(options->media.size_name);
    if (!paper_code) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unsupported paper size '%s'.", options->media.size_name);
        job_data->failed = true;
        return false;
    }

    if (job_data->pages == 0) {
        job_data->width = (int)options->header.cupsWidth;
        job_data->height = (int)options->header.cupsHeight;
        job_data->xres = options->printer_resolution[0];
        job_data->yres = options->printer_resolution[1];
        job_data->paper_code = paper_code;
        job_data->copies = options->copies > 0 ? options->copies : 1;
    } else if (job_data->width != (int)options->header.cupsWidth ||
               job_data->height != (int)options->header.cupsHeight ||
               job_data->xres != options->printer_resolution[0] ||
               job_data->yres != options->printer_resolution[1] ||
               job_data->paper_code != paper_code) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Mixed page geometry is not enabled in the dry-run renderer.");
        job_data->failed = true;
        return false;
    }

    if (fprintf(job_data->pbm, "P4\n%u %u\n",
                options->header.cupsWidth,
                options->header.cupsHeight) < 0) {
        job_data->failed = true;
        return false;
    }

    job_data->rows_written = 0;
    return true;
}

static bool raster_rwriteline(pappl_job_t *job,
                              pappl_pr_options_t *options,
                              pappl_device_t *device,
                              unsigned y,
                              const unsigned char *line)
{
    p1102_job_data_t *job_data =
        (p1102_job_data_t *)papplJobGetData(job);
    size_t row_bytes;

    (void)device;
    (void)y;

    if (!job_data || !job_data->pbm || job_data->failed)
        return false;

    row_bytes = ((size_t)options->header.cupsWidth + 7u) / 8u;
    if (row_bytes > (size_t)options->header.cupsBytesPerLine) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Raster line is shorter than the PBM row.");
        job_data->failed = true;
        return false;
    }

    if (fwrite(line, 1, row_bytes, job_data->pbm) != row_bytes) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to write temporary PBM raster data.");
        job_data->failed = true;
        return false;
    }

    job_data->rows_written++;
    return true;
}

static bool raster_rendpage(pappl_job_t *job,
                            pappl_pr_options_t *options,
                            pappl_device_t *device,
                            unsigned page)
{
    p1102_job_data_t *job_data =
        (p1102_job_data_t *)papplJobGetData(job);

    (void)options;
    (void)device;
    (void)page;

    if (!job_data || job_data->failed)
        return false;

    if (job_data->rows_written != job_data->height) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Raster page ended after %d of %d rows.",
                    job_data->rows_written, job_data->height);
        job_data->failed = true;
        return false;
    }

    job_data->pages++;
    return true;
}

static bool encode_pbm_to_zjs(pappl_job_t *job, p1102_job_data_t *job_data)
{
    char resolution[32];
    char geometry[32];
    char paper[16];
    char copies[16];
    int in_fd;
    int out_fd;
    pid_t pid;
    int status;
    struct stat st;

    if (fflush(job_data->pbm) != 0 || fclose(job_data->pbm) != 0) {
        job_data->pbm = NULL;
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to finish temporary PBM file.");
        return false;
    }
    job_data->pbm = NULL;

    snprintf(job_data->zjs_path, sizeof(job_data->zjs_path),
             "/tmp/hplp-p1102-zjs-XXXXXX");
    out_fd = mkstemp(job_data->zjs_path);
    if (out_fd < 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to create temporary ZJS file.");
        return false;
    }

    in_fd = open(job_data->pbm_path, O_RDONLY);
    if (in_fd < 0) {
        close(out_fd);
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to reopen temporary PBM file.");
        return false;
    }

    snprintf(resolution, sizeof(resolution), "-r%dx%d",
             job_data->xres, job_data->yres);
    snprintf(geometry, sizeof(geometry), "-g%dx%d",
             job_data->width, job_data->height);
    snprintf(paper, sizeof(paper), "-p%d", job_data->paper_code);
    snprintf(copies, sizeof(copies), "-n%d", job_data->copies);

    pid = fork();
    if (pid < 0) {
        close(in_fd);
        close(out_fd);
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "Unable to start foo2zjs.");
        return false;
    }

    if (pid == 0) {
        if (dup2(in_fd, STDIN_FILENO) < 0 ||
            dup2(out_fd, STDOUT_FILENO) < 0)
            _exit(126);

        close(in_fd);
        close(out_fd);

        execl("/usr/bin/foo2zjs",
              "foo2zjs",
              resolution,
              geometry,
              paper,
              "-m1",
              copies,
              "-d1",
              "-s7",
              "-z2",
              "-L0",
              "-P",
              (char *)NULL);
        _exit(127);
    }

    close(in_fd);
    close(out_fd);

    if (waitpid(pid, &status, 0) < 0 ||
        !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "foo2zjs failed while creating the dry-run ZJS stream.");
        return false;
    }

    if (stat(job_data->zjs_path, &st) != 0 || st.st_size <= 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR,
                    "foo2zjs produced an empty ZJS stream.");
        return false;
    }

    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "DRY-RUN ZJS OK: pages=%d bytes=%lld usb-data-sent=no",
                job_data->pages, (long long)st.st_size);
    return true;
}

static bool raster_rendjob(pappl_job_t *job,
                           pappl_pr_options_t *options,
                           pappl_device_t *device)
{
    p1102_job_data_t *job_data =
        (p1102_job_data_t *)papplJobGetData(job);
    bool encoded = false;

    (void)options;
    (void)device;

    if (job_data && !job_data->failed && job_data->pages > 0)
        encoded = encode_pbm_to_zjs(job, job_data);

    cleanup_job_data(job_data);
    papplJobSetData(job, NULL);

    if (encoded) {
        papplLogJob(job, PAPPL_LOGLEVEL_WARN,
                    "Dry-run only: ZJS was validated but deliberately not sent to USB.");
    }

    /*
     * Deliberately fail the job in this milestone so the UI never reports
     * a physical print that did not actually happen.
     */
    return false;
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
    driver_data->rstartjob_cb = raster_rstartjob;
    driver_data->rstartpage_cb = raster_rstartpage;
    driver_data->rwriteline_cb = raster_rwriteline;
    driver_data->rendpage_cb = raster_rendpage;
    driver_data->rendjob_cb = raster_rendjob;

    driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;
    driver_data->quality_default = IPP_QUALITY_NORMAL;
    driver_data->orient_default = IPP_ORIENT_NONE;
    driver_data->scaling_default = PAPPL_SCALING_AUTO;

    driver_data->raster_types =
        PAPPL_PWG_RASTER_TYPE_BLACK_1 |
        PAPPL_PWG_RASTER_TYPE_BLACK_8 |
        PAPPL_PWG_RASTER_TYPE_SGRAY_8;
    driver_data->force_raster_type = PAPPL_PWG_RASTER_TYPE_BLACK_1;

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
