<?php
declare(strict_types=1);
require __DIR__.'/lib.php';
app_boot(); security_headers(true);
function answer(array $value,int $status=200): void {http_response_code($status);echo json_encode($value,JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES);exit;}
function items(): array {
 $rows=[];$stats=json_read(STATS_FILE);
 foreach(categories() as $category){$directory=STORAGE_DIR.'/'.$category;if(!is_dir($directory))continue;foreach(new DirectoryIterator($directory) as $entry){if($entry->isDot()||!$entry->isFile()||str_starts_with($entry->getFilename(),'.'))continue;$name=$entry->getFilename();$ext=strtolower(pathinfo($name,PATHINFO_EXTENSION));$id=hash('sha256',$category.'/'.$name);$rows[]=['id'=>$id,'name'=>$name,'category'=>$category,'kind'=>in_array($ext,['png','jpg','jpeg','webp','gif'],true)?'image':(in_array($ext,['mp4','webm','mov'],true)?'video':'text'),'url'=>'storage/'.rawurlencode($category).'/'.rawurlencode($name),'thumbnail'=>'kalwer.php?action=thumbnail&id='.$id,'mtime'=>$entry->getMTime(),'count'=>(int)($stats[$id]['count']??0),'path'=>$entry->getPathname()];if(count($rows)>=5000)break 2;}}
 usort($rows,fn($a,$b)=>$b['mtime']<=>$a['mtime'] ?: strcmp($a['name'],$b['name']));return $rows;
}
function authenticate(): void {
 $token=(string)($_SERVER['HTTP_X_KALWER_UPLOAD_KEY']??'');$file=APP_ROOT.'/.kalwer-upload-keys.json';$keys=is_file($file)?json_decode((string)file_get_contents($file),true):[];$ok=false;
 if(preg_match('/^[a-f0-9]{64}$/D',$token))foreach(($keys['keys']??[]) as $entry)if(is_string($entry['hash']??null)&&hash_equals($entry['hash'],hash('sha256',$token)))$ok=true;
 if(!$ok)answer(['ok'=>false,'error'=>'Configure a valid upload key in Kalwer.'],401);
}
function thumbnail(array $item): void {
 $cache=DATA_DIR.'/kalwer-thumbnails';if(!is_dir($cache))@mkdir($cache,0750,true);$target=$cache.'/'.$item['id'].'-'.$item['mtime'].'.jpg';
 if(!is_file($target)&&$item['kind']==='image'&&function_exists('imagecreatetruecolor')&&filesize($item['path'])<=16*1024*1024){$size=@getimagesize($item['path']);if($size&&$size[0]*$size[1]<=16000000){$type=$size[2];$image=match($type){IMAGETYPE_JPEG=>@imagecreatefromjpeg($item['path']),IMAGETYPE_PNG=>@imagecreatefrompng($item['path']),IMAGETYPE_GIF=>@imagecreatefromgif($item['path']),IMAGETYPE_WEBP=>function_exists('imagecreatefromwebp')?@imagecreatefromwebp($item['path']):false,default=>false};if($image){$factor=min(1,240/max($size[0],$size[1]));$w=max(1,(int)round($size[0]*$factor));$h=max(1,(int)round($size[1]*$factor));$small=imagecreatetruecolor($w,$h);imagecopyresampled($small,$image,0,0,0,0,$w,$h,$size[0],$size[1]);$tmp=$target.'.'.bin2hex(random_bytes(4)).'.tmp';if(imagejpeg($small,$tmp,80))rename($tmp,$target);imagedestroy($small);imagedestroy($image);}}}
 header('Cache-Control: public, max-age=86400');
 if(is_file($target)){header('Content-Type: image/jpeg');readfile($target);exit;}
 if($item['kind']==='image'&&filesize($item['path'])<=2*1024*1024){header('Content-Type: '.(mime_content_type($item['path'])?:'image/png'));readfile($item['path']);exit;}
 header('Content-Type: image/png');echo base64_decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/l9sAAAAASUVORK5CYII=');exit;
}
try {
 $action=(string)($_GET['action']??'list');
 if($action==='upload'){
  if($_SERVER['REQUEST_METHOD']!=='POST')answer(['ok'=>false,'error'=>'POST required'],405);authenticate();
  $category=valid_category((string)($_POST['category']??''));if($category===null||!is_dir(STORAGE_DIR.'/'.$category))answer(['ok'=>false,'error'=>'Choose an existing category.'],400);
  $file=$_FILES['file']??null;if(!is_array($file)||($file['error']??UPLOAD_ERR_NO_FILE)!==UPLOAD_ERR_OK||!is_uploaded_file((string)($file['tmp_name']??'')))answer(['ok'=>false,'error'=>'Choose a readable file.'],400);
  if((int)$file['size']<1||(int)$file['size']>MAX_UPLOAD_BYTES)answer(['ok'=>false,'error'=>'Files must be between 1 byte and 200 MiB.'],400);
  $name=safe_filename((string)$file['name']);$ext=strtolower(pathinfo($name,PATHINFO_EXTENSION));if(allowed_upload((string)$file['tmp_name'],$ext)===null)answer(['ok'=>false,'error'=>'That file type or content is not supported.'],400);
  if(($_POST['validate_only']??'')==='1')answer(['ok'=>true,'validated'=>true]);
  $base=trim((string)($_POST['name']??''));if($base==='')$base=pathinfo($name,PATHINFO_FILENAME);$target=unique_destination(STORAGE_DIR.'/'.$category,safe_filename($base),$ext);
  if(!move_uploaded_file((string)$file['tmp_name'],$target))throw new RuntimeException();answer(['ok'=>true,'name'=>basename($target),'url'=>'storage/'.rawurlencode($category).'/'.rawurlencode(basename($target))]);
 }
 $rows=items();if($action==='thumbnail'){$id=(string)($_GET['id']??'');foreach($rows as $item)if(hash_equals($item['id'],$id))thumbnail($item);answer(['ok'=>false,'error'=>'Meme unavailable.'],404);}
 if($action!=='list')answer(['ok'=>false,'error'=>'Unknown action.'],400);
 $category=(string)($_GET['cat']??'all');$query=trim((string)($_GET['q']??''));$rows=array_values(array_filter($rows,function($i)use($category,$query){return($category==='all'||$i['category']===$category)&&($query===''||preg_match('/'.preg_quote($query,'/').'/iu',$i['name'].' '.$i['category'])===1);}));$total=count($rows);$offset=max(0,(int)($_GET['offset']??0));$slice=array_slice($rows,$offset,48);foreach($slice as &$item)unset($item['path']);unset($item);answer(['ok'=>true,'items'=>$slice,'categories'=>categories(),'next'=>$offset+count($slice)<$total?$offset+count($slice):null,'total'=>$total]);
}catch(Throwable $e){answer(['ok'=>false,'error'=>'The request could not be completed.'],500);}
