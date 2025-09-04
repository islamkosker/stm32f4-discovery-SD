 // Zephyr includes
 #include <zephyr/device.h>
 #include <zephyr/devicetree.h>
 #include <zephyr/fs/fs.h>
 #include <zephyr/logging/log.h>
 
 // FatFS include
 #include <ff.h>
 
 LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);
 
 #define SD_LABEL "SD0"
 
 int mounted;
 int rc;
 
 static FATFS fatfs;
 static struct fs_mount_t sd_mnt = {
     .type = FS_FATFS,
     .mnt_point = "/SDHC:",
     .fs_data = &fatfs,
     .storage_dev = (void *)SD_LABEL,
     .flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS,
 };
 
 int main(void)
 {
     rc = fs_mount(&sd_mnt);
     if (rc < 0)
     {
         LOG_ERR("fs_mount: %d", rc);
         return rc;
     }
 
     struct fs_file_t f;
     fs_file_t_init(&f);
 
     rc = fs_open(&f, "/SDHC:/test.bin", FS_O_CREATE | FS_O_WRITE);
     if (rc < 0)
     {
         LOG_ERR("fs_open: %d", rc);
         return rc;
     }
 
     static uint8_t buf[512] = {0};
     (void)fs_write(&f, buf, sizeof(buf));
     (void)fs_sync(&f);
     (void)fs_close(&f);
     LOG_INF("Write OK");
     if (rc < 0)
     {
         mounted = 0;
     }
     else
     {
         mounted = 1;
     }
     return 0;
 }