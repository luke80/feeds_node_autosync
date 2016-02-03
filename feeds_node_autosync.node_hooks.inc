<?php

/**
 * Implements hook_node_update().
 *
 * Main engine of the autosync. Adds the automatic import request to node update events via the hook.
 *
 * @param node
 *   Contains the node which was updated (the update that fired the hook)
*/
function feeds_node_autosync_node_update( $node ) {
	$c = feeds_node_autosync_config($node->type);		//	Load the configuration settings for this node type!
	//	watchdog('feeds_node_autosync',"hook node_update fired!",NULL,WATCHDOG_DEBUG);	//	Perhaps we should log this kind of thing?
	if($c->config->active->value == 1) {	//	If this node type is set to actively auto-sync...
		//	Retrieve the feeds_item
		$feeds_items_result = db_select('feeds_item', 'n')
			->fields('n', array('id', 'url', 'entity_id', 'imported'))
			->condition('entity_id', $node->nid)
			->condition('entity_type', 'node')	//	Conditions deliberately not matching against feed_importer_id (expecting there to be a 1st one that imports all and a 2nd one that is used to auto-sync)
			->orderby('imported','DESC')		//	Newest (most recent) first
			->range(0,1)		//	But only give me the one...
			->execute();
		$feeds_items_row = $feeds_items_result->fetchAssoc();
		unset($feeds_items_result);
		//	If the request time minus the import timestamp (age in seconds since last request) is greater than the tolernace setting, perform an import!
		if( (REQUEST_TIME - $feeds_items_row['imported']) > $c->config->cooldown->value ) {
			$feed_source = feeds_source($c->config->importer->value);
			$feed_config = $feed_source->getConfig();
			$feed_config['FeedsHTTPFetcher']['source'] = $feeds_items_row['url'];
			//	This used to ensure that the node-configured feed source was the URL stored in the feeds_item table, but that is redundant.
			//$node->feeds['FeedsHTTPFetcher']['source'] = $feeds_items_row['url'];
			//node_save($node);
			$feed_source->setConfig($feed_config);
			unset($feed_config);
			$feed_source->save();
			//	Optional message to alter the user that the update has begun?
			//	drupal_set_message(t('Updating %node_title from feed source.',array('%node_title'=>$node->title)),'status',TRUE);
			$feed_source->startImport();
			//	I saw somewhere that this would perform the batch without the ajax thing... but I found it worked less reliably. The limitation seems to be that only a small number of feeds can be processed at the same time, and that limit is hit far too often to do it this way.
			//	while (FEEDS_BATCH_COMPLETE != $feed_source->import());
			unset($feed_source);
		} else {
			//	Since updating the node fires this update hook, a second event is inevitable. This event verifies that the node has been updated indeed, therefore show the updated from source message.
			drupal_set_message(t('%node_title updated from feed source.',array('%node_title'=>$node->title)),'status',TRUE);
		}
		unset($feeds_items_row);
	}
	unset($c);
}

