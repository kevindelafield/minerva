
$( function(){
    // dataString = '{\n"SSID": "' + '11' + '",\n"encryptionType":"'+ '22'+'",\n"algorithm": "' + '33' + '",\n"key":"' + '44' + '"\n}'
    // alert(dataString)
    $('#manually_div').hide();
    $('#scan_div').show();

    //var title_menu = '<li class="list-group-item disabled" aria-disabled="true"><div class="row"><div class="col-6"><label class="btn-sm">Network Name</label></div><div class="col-2"><label class="btn-sm">Security</label></div><div class="col"></div><label class="btn-sm">Refresh</label></div></li>';
             
    var loader = '<div class="d-flex justify-content-center"><div class="spinner-border mt-5" role="status"><span class="sr-only" >Loading...</span></div></div>';
    var wifi_icon = '<img src="./img/wifi.png" alt="))))" height="32" width="32">'
	var retryCount = 0;
    $("#UPGRADE").click(function(){
        
        window.location.href = "/fw_upgrade.html";
    });
    
    $("#SETUP_MANUALLY").click(function(){
        
        $('#manually_div').show();
        $('#scan_div').hide();

    });
    function Check_Time(){
        $('#ap_list').html(''); 
    }
    
    $("#refresh_button").click(function(){

        // var ap ='<button class="list-group-item list-group-item-action" id="'+ 'div_id' +'"><div class="row"><div class="col-6"><label class="col-md-4 btn-sm">'+ 'div_iddddddddddddddddddddddddddddddddddddddd' +'</label></div><div class="col-2"><label class="btn-sm">'+ '22' +'</label></div><div class="col"></div>'+ wifi_icon + '</div></li>';              
        // $('#ap_list').append(ap);
        refresh();
    });
    $("#SCAN_NETWORKS").click(function(){
        $('#scan_div').show();
        $('#manually_div').hide();
        refresh();
    });

    function refresh(){
        $('#ap_list').html(''); 
        $('#ap_list').append(loader);
        
        // for(var i= 0 ; i<30 ; i++){
        //     var ap = '<button class="list-group-item list-group-item-action" id="ap_'+ i +'"><div class="row"><div class="col">AP_'+ i +'</div><div class="col-2">WEP</div><div class="col-2">))))</div></div></button>'
        //     $('#ap_list').append(ap);
        //     bindApInfo(i);
        // }
        // window.setInterval(Check_Time, 30000);
        $.ajax({
            url: '/wifi/ScanForWirelessNetworks',
            // data: {
            //   'project_name': 'CK',
            // },
            dataType: 'json',
            success: function (data) {
                $('#ap_list').html(''); 
				retryCount = 0;
                // alert('data');
                var i = 0;
                 data['result'].forEach(function(element) {

                    //var ap ='<button class="list-group-item list-group-item-action" id="'+ element['SSID'] +'"><div class="row"><label class="btn-sm">'+ element['SSID'] +'</label><div class="col-md"></div><label class="btn-sm">' + element['encryptionType'] + '</label><div class="col-2"></div><label class="btn-sm">))))</label></div></button>'
                    div_id = 'SSID_'+ i;
                    var ap ='<button class="list-group-item list-group-item-action" id="'+ div_id +'"><div class="row"><div class="col-6"><label class="col-md-4 btn-sm">'+ element['SSID'] +'</label></div><div class="col-2"><label class="btn-sm">'+ element['encryptionType'] +'</label></div><div class="col"></div>'+ element['signalStrength'] + '%</div></li>';
                            
					$('#ap_list').append(ap);
                    bindApInfo(element, div_id);
                    i++;
                    // $('#loading_icon').hide();
                });
                
            },
			error: function(xhr, ajaxOptions, throwError) {
				console.log(xhr.responseText);
				
				if(retryCount < 3){
					refresh();
					retryCount++;
				}
				
			}
        });



    }
    $("#manually_join_button").click(function(){
        
         var ssid =  $("#ssid_field").val();
         var password =  $("#password_field").val();
         var security =  $("#security_field").val();
        //  alert(ssid+password+security);

         dataString = '{\n"SSID": "' + ssid + '",\n"encryptionType":"'+ security+'",\n"key":"' + password +'"\n}'
         $.ajax({
            type: 'POST',
            url: '/wifi/SetWirelessSettings',
            // data: JSON.stringify({
            //     "SSID": ssid,
            //     "encryptionType": security,
            //     // "algorithm": element['algorithm'],
            //     'key': password
            // }),
            data: dataString,
            dataType: 'json',
            success: function (data) {

                if(data['errorCode'] == 0){
                    // alert('Connecting to your AP ... and refer to your quick installation guide to proceed with the next step.')
                    window.location.assign("result.html");
                }else{
                    alert('Error:'+ data['errorCode'])
                }
                // alert('data');
    
            },
			error: function(xhr, ajaxOptions, throwError) {
				console.log(xhr.responseText);
			}
        });
    });

   function bindApInfo(element, div_id){
    $(String('#'+ div_id)).bind("click",function(){
        // alert(num);
        $('#exampleModalCenterTitle').html('The Wi-Fi network '+ element['SSID'] +' requires a password')

        $('#join_button').bind("click",function(){
            
            var ct = $('#chimeType option:selected').val();
            
            var json = '{ "chimeType" : ' + ct + ' }';
            
            $.ajax({
                type: 'POST',
                url: '/wifi/SetChimeType',
                data: json,
                dataType: 'json',
                success: function (data)
                {
                    if (data['errorCode'] == 0)
                    {
			            var ssid = element['SSID'].replace(/\\/g, "\\\\");
                        
                        dataString = '{\n"SSID": "' + ssid + '",\n"encryptionType":"'+ element['encryptionType']+'",\n"algorithm": "' + element['algorithm'] + '",\n"key":"' + $('#InputPassword').val() + '"\n}'
                        $.ajax({
                            type: 'POST',
                            url: '/wifi/SetWirelessSettings',
                            // data: JSON.stringify({
                            //     "SSID": element['SSID'],
                            //     "encryptionType": element['encryptionType'],
                            //     "algorithm": element['algorithm'],
                            //     'key': $('#InputPassword').val()
                            // }),
                            data: dataString,
                            dataType: 'json',
                            success: function (data) {
                                
                                if(data['errorCode'] == 0){
                                    //alert('Connecting to your AP ... and refer to your quick installation guide to proceed with the next step.')
                                    window.location.assign("result.html");
                                }else{
                                    alert('Error:'+ data['errorCode'])
                                }
                                $('#exampleModalCenter').modal('hide')
                                // alert('data');
                                
                            }
                        });
                        
                    }
                    else
                    {
                        alert('Error saving chime type');
                    }
                }
            });
            
        });
        $('#exampleModalCenter').modal('show')
    });

   }
});
