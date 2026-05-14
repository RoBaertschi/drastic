LIMINE_REQUEST
static volatile struct limine_rsdp_request system_acpi_rsdp_request = {
    .id       = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

typedef struct PACKED System_Acpi_Rsdp_Descriptor {
    U8  signature[8]; // "RSD PTR "
    U8  checksum;
    U8  oem_id[6];
    U8  revision;
    U32 rsdt_address;
} System_Acpi_Rsdp_Descriptor;

typedef struct PACKED System_Acpi_Xsdp_Descriptor {
    U8  signature[8]; // "RSD PTR "
    U8  checksum;
    U8  oem_id[6];
    U8  revision;     // 2
    U32 rsdt_address;

    U32 length;
    U64 xsdt_address;
    U8  extended_checksum;
    U8  reserved[3];
} System_Acpi_Xsdp_Descriptor;

typedef struct PACKED System_Acpi_Sdt_Header {
    U8  signature[4];
    U32 length;
    U8  revision;
    U8  checksum;
    U8  oem_id[6];
    U8  oem_table_id;
    U32 oem_revision;
    U32 creator_id;
    U32 creater_revision;
} System_Acpi_Sdt_Header;

internal bool system_acpi_rsdp_validate(Bytes rsdp_data) {
    U32 result = 0;
    for (Int i = 0; i < rsdp_data.len; i++) {
        result += bytes_get(rsdp_data, i);
    }
    return (result & 0xFF) == 0;
}

internal void system_acpi_setup(void) {
    kassert(system_acpi_rsdp_request.revision == 0);
    kassert(system_acpi_rsdp_request.response != NULL);
    kassert(system_acpi_rsdp_request.response->revision == 0);

    System_Acpi_Rsdp_Descriptor *rsdp = system_acpi_rsdp_request.response->address;

    if (!system_acpi_rsdp_validate(BYTES_FROM_PTR(rsdp))) {
        kpanic(STR("rsdp checksum failed"));
    }

    System_Acpi_Xsdp_Descriptor *xsdp = NULL;
    if (rsdp->revision == 2) {
        xsdp = (System_Acpi_Xsdp_Descriptor *)rsdp;
        if (!system_acpi_rsdp_validate(BYTES_FROM_PTR(xsdp))) {
            kpanic(STR("xsdp checksum failed"));
        }
    }

    void *rsdt_address = (void *)(Uintptr)rsdp->rsdt_address;
    if (xsdp) {
        rsdt_address = (void *)xsdp->xsdt_address;
        printf("found xsdt at %p\n", rsdt_address);
    } else {
        printf("found rsdt at %p\n", rsdt_address);
    }
}
